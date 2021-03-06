///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016 Edouard Griffiths, F4EXB                                   //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#include "ammod.h"

#include <QTime>
#include <QDebug>
#include <QMutexLocker>
#include <stdio.h>
#include <complex.h>
#include <dsp/upchannelizer.h>
#include "dsp/dspengine.h"
#include "dsp/pidcontroller.h"
#include "dsp/threadedbasebandsamplesource.h"
#include "device/devicesinkapi.h"

MESSAGE_CLASS_DEFINITION(AMMod::MsgConfigureAMMod, Message)
MESSAGE_CLASS_DEFINITION(AMMod::MsgConfigureChannelizer, Message)
MESSAGE_CLASS_DEFINITION(AMMod::MsgConfigureFileSourceName, Message)
MESSAGE_CLASS_DEFINITION(AMMod::MsgConfigureFileSourceSeek, Message)
MESSAGE_CLASS_DEFINITION(AMMod::MsgConfigureAFInput, Message)
MESSAGE_CLASS_DEFINITION(AMMod::MsgConfigureFileSourceStreamTiming, Message)
MESSAGE_CLASS_DEFINITION(AMMod::MsgReportFileSourceStreamData, Message)
MESSAGE_CLASS_DEFINITION(AMMod::MsgReportFileSourceStreamTiming, Message)

const QString AMMod::m_channelIdURI = "sdrangel.channeltx.modam";
const QString AMMod::m_channelId ="AMMod";
const int AMMod::m_levelNbSamples = 480; // every 10ms

AMMod::AMMod(DeviceSinkAPI *deviceAPI) :
    m_deviceAPI(deviceAPI),
    m_absoluteFrequencyOffset(0),
    m_movingAverage(40, 0),
    m_volumeAGC(40, 0),
    m_audioFifo(4800),
	m_settingsMutex(QMutex::Recursive),
	m_fileSize(0),
	m_recordLength(0),
	m_sampleRate(48000),
	m_afInput(AMModInputNone),
	m_levelCalcCount(0),
	m_peakLevel(0.0f),
	m_levelSum(0.0f)
{
	setObjectName(m_channelId);

	m_audioBuffer.resize(1<<14);
	m_audioBufferFill = 0;

	m_movingAverage.resize(16, 0);
	m_volumeAGC.resize(4096, 0.003, 0);
	m_magsq = 0.0;

	m_toneNco.setFreq(1000.0, m_settings.m_audioSampleRate);
	DSPEngine::instance()->addAudioSource(&m_audioFifo);

	// CW keyer
	m_cwKeyer.setSampleRate(m_settings.m_audioSampleRate);
	m_cwKeyer.setWPM(13);
	m_cwKeyer.setMode(CWKeyer::CWNone);

    m_channelizer = new UpChannelizer(this);
    m_threadedChannelizer = new ThreadedBasebandSampleSource(m_channelizer, this);
    m_deviceAPI->addThreadedSource(m_threadedChannelizer);
    m_deviceAPI->addChannelAPI(this);

    applySettings(m_settings, true);
}

AMMod::~AMMod()
{
    m_deviceAPI->removeChannelAPI(this);
    m_deviceAPI->removeThreadedSource(m_threadedChannelizer);
    delete m_threadedChannelizer;
    delete m_channelizer;
    DSPEngine::instance()->removeAudioSource(&m_audioFifo);
}

void AMMod::pull(Sample& sample)
{
	if (m_settings.m_channelMute)
	{
		sample.m_real = 0.0f;
		sample.m_imag = 0.0f;
		return;
	}

	Complex ci;

	m_settingsMutex.lock();

    if (m_interpolatorDistance > 1.0f) // decimate
    {
    	modulateSample();

        while (!m_interpolator.decimate(&m_interpolatorDistanceRemain, m_modSample, &ci))
        {
        	modulateSample();
        }
    }
    else
    {
        if (m_interpolator.interpolate(&m_interpolatorDistanceRemain, m_modSample, &ci))
        {
        	modulateSample();
        }
    }

    m_interpolatorDistanceRemain += m_interpolatorDistance;

    ci *= m_carrierNco.nextIQ(); // shift to carrier frequency

    m_settingsMutex.unlock();

    Real magsq = ci.real() * ci.real() + ci.imag() * ci.imag();
	magsq /= (1<<30);
	m_movingAverage.feed(magsq);
	m_magsq = m_movingAverage.average();

	sample.m_real = (FixReal) ci.real();
	sample.m_imag = (FixReal) ci.imag();
}

void AMMod::pullAudio(int nbSamples)
{
//    qDebug("AMMod::pullAudio: %d", nbSamples);
    unsigned int nbAudioSamples = nbSamples * ((Real) m_settings.m_audioSampleRate / (Real) m_settings.m_basebandSampleRate);

    if (nbAudioSamples > m_audioBuffer.size())
    {
        m_audioBuffer.resize(nbAudioSamples);
    }

    m_audioFifo.read(reinterpret_cast<quint8*>(&m_audioBuffer[0]), nbAudioSamples, 10);
    m_audioBufferFill = 0;
}

void AMMod::modulateSample()
{
	Real t;

    pullAF(t);
    calculateLevel(t);
    m_audioBufferFill++;

    m_modSample.real((t*m_settings.m_modFactor + 1.0f) * 16384.0f); // modulate and scale zero frequency carrier
    m_modSample.imag(0.0f);
}

void AMMod::pullAF(Real& sample)
{
    switch (m_afInput)
    {
    case AMModInputTone:
        sample = m_toneNco.next();
        break;
    case AMModInputFile:
        // sox f4exb_call.wav --encoding float --endian little f4exb_call.raw
        // ffplay -f f32le -ar 48k -ac 1 f4exb_call.raw
        if (m_ifstream.is_open())
        {
            if (m_ifstream.eof())
            {
            	if (m_settings.m_playLoop)
            	{
                    m_ifstream.clear();
                    m_ifstream.seekg(0, std::ios::beg);
            	}
            }

            if (m_ifstream.eof())
            {
            	sample = 0.0f;
            }
            else
            {
            	m_ifstream.read(reinterpret_cast<char*>(&sample), sizeof(Real));
            	sample *= m_settings.m_volumeFactor;
            }
        }
        else
        {
            sample = 0.0f;
        }
        break;
    case AMModInputAudio:
        sample = ((m_audioBuffer[m_audioBufferFill].l + m_audioBuffer[m_audioBufferFill].r) / 65536.0f) * m_settings.m_volumeFactor;
        break;
    case AMModInputCWTone:
        Real fadeFactor;

        if (m_cwKeyer.getSample())
        {
            m_cwKeyer.getCWSmoother().getFadeSample(true, fadeFactor);
            sample = m_toneNco.next() * fadeFactor;
        }
        else
        {
            if (m_cwKeyer.getCWSmoother().getFadeSample(false, fadeFactor))
            {
                sample = m_toneNco.next() * fadeFactor;
            }
            else
            {
                sample = 0.0f;
                m_toneNco.setPhase(0);
            }
        }
        break;
    case AMModInputNone:
    default:
        sample = 0.0f;
        break;
    }
}

void AMMod::calculateLevel(Real& sample)
{
    if (m_levelCalcCount < m_levelNbSamples)
    {
        m_peakLevel = std::max(std::fabs(m_peakLevel), sample);
        m_levelSum += sample * sample;
        m_levelCalcCount++;
    }
    else
    {
        qreal rmsLevel = sqrt(m_levelSum / m_levelNbSamples);
        //qDebug("NFMMod::calculateLevel: %f %f", rmsLevel, m_peakLevel);
        emit levelChanged(rmsLevel, m_peakLevel, m_levelNbSamples);
        m_peakLevel = 0.0f;
        m_levelSum = 0.0f;
        m_levelCalcCount = 0;
    }
}

void AMMod::start()
{
	qDebug() << "AMMod::start: m_outputSampleRate: " << m_settings.m_outputSampleRate
			<< " m_inputFrequencyOffset: " << m_settings.m_inputFrequencyOffset;

	m_audioFifo.clear();
}

void AMMod::stop()
{
}

bool AMMod::handleMessage(const Message& cmd)
{
	if (UpChannelizer::MsgChannelizerNotification::match(cmd))
	{
		UpChannelizer::MsgChannelizerNotification& notif = (UpChannelizer::MsgChannelizerNotification&) cmd;

		AMModSettings settings = m_settings;

		settings.m_basebandSampleRate = notif.getBasebandSampleRate();
		settings.m_outputSampleRate = notif.getSampleRate();
		settings.m_inputFrequencyOffset = notif.getFrequencyOffset();

		applySettings(settings);

		qDebug() << "AMMod::handleMessage: MsgChannelizerNotification:"
				<< " m_basebandSampleRate: " << settings.m_basebandSampleRate
                << " m_outputSampleRate: " << settings.m_outputSampleRate
				<< " m_inputFrequencyOffset: " << settings.m_inputFrequencyOffset;

		return true;
	}
    else if (MsgConfigureChannelizer::match(cmd))
    {
        MsgConfigureChannelizer& cfg = (MsgConfigureChannelizer&) cmd;

        m_channelizer->configure(m_channelizer->getInputMessageQueue(),
            cfg.getSampleRate(),
            cfg.getCenterFrequency());

        qDebug() << "AMMod::handleMessage: MsgConfigureChannelizer:"
                << " getSampleRate: " << cfg.getSampleRate()
                << " getCenterFrequency: " << cfg.getCenterFrequency();

        return true;
    }
    else if (MsgConfigureAMMod::match(cmd))
    {
        MsgConfigureAMMod& cfg = (MsgConfigureAMMod&) cmd;

        AMModSettings settings = cfg.getSettings();

        // These settings are set with DownChannelizer::MsgChannelizerNotification
        m_absoluteFrequencyOffset = settings.m_inputFrequencyOffset;
        settings.m_basebandSampleRate = m_settings.m_basebandSampleRate;
        settings.m_outputSampleRate = m_settings.m_outputSampleRate;
        settings.m_inputFrequencyOffset = m_settings.m_inputFrequencyOffset;

        applySettings(settings, cfg.getForce());

        qDebug() << "AMMod::handleMessage: MsgConfigureAMMod:"
                << " m_basebandSampleRate: " << settings.m_basebandSampleRate
                << " m_outputSampleRate: " << settings.m_outputSampleRate
                << " m_inputFrequencyOffset: " << settings.m_inputFrequencyOffset
                << " m_rfBandwidth: " << settings.m_rfBandwidth
                << " m_modFactor: " << settings.m_modFactor
                << " m_toneFrequency: " << settings.m_toneFrequency
                << " m_volumeFactor: " << settings.m_volumeFactor
                << " m_audioMute: " << settings.m_channelMute
                << " m_playLoop: " << settings.m_playLoop;

        return true;
    }
	else if (MsgConfigureFileSourceName::match(cmd))
    {
        MsgConfigureFileSourceName& conf = (MsgConfigureFileSourceName&) cmd;
        m_fileName = conf.getFileName();
        openFileStream();
        return true;
    }
    else if (MsgConfigureFileSourceSeek::match(cmd))
    {
        MsgConfigureFileSourceSeek& conf = (MsgConfigureFileSourceSeek&) cmd;
        int seekPercentage = conf.getPercentage();
        seekFileStream(seekPercentage);

        return true;
    }
    else if (MsgConfigureAFInput::match(cmd))
    {
        MsgConfigureAFInput& conf = (MsgConfigureAFInput&) cmd;
        m_afInput = conf.getAFInput();

        return true;
    }
    else if (MsgConfigureFileSourceStreamTiming::match(cmd))
    {
    	std::size_t samplesCount;

    	if (m_ifstream.eof()) {
    		samplesCount = m_fileSize / sizeof(Real);
    	} else {
    		samplesCount = m_ifstream.tellg() / sizeof(Real);
    	}

    	MsgReportFileSourceStreamTiming *report;
        report = MsgReportFileSourceStreamTiming::create(samplesCount);
        getMessageQueueToGUI()->push(report);

        return true;
    }
	else
	{
		return false;
	}
}

void AMMod::openFileStream()
{
    if (m_ifstream.is_open()) {
        m_ifstream.close();
    }

    m_ifstream.open(m_fileName.toStdString().c_str(), std::ios::binary | std::ios::ate);
    m_fileSize = m_ifstream.tellg();
    m_ifstream.seekg(0,std::ios_base::beg);

    m_sampleRate = 48000; // fixed rate
    m_recordLength = m_fileSize / (sizeof(Real) * m_sampleRate);

    qDebug() << "AMMod::openFileStream: " << m_fileName.toStdString().c_str()
            << " fileSize: " << m_fileSize << "bytes"
            << " length: " << m_recordLength << " seconds";

    MsgReportFileSourceStreamData *report;
    report = MsgReportFileSourceStreamData::create(m_sampleRate, m_recordLength);
    getMessageQueueToGUI()->push(report);
}

void AMMod::seekFileStream(int seekPercentage)
{
    QMutexLocker mutexLocker(&m_settingsMutex);

    if (m_ifstream.is_open())
    {
        int seekPoint = ((m_recordLength * seekPercentage) / 100) * m_sampleRate;
        seekPoint *= sizeof(Real);
        m_ifstream.clear();
        m_ifstream.seekg(seekPoint, std::ios::beg);
    }
}

void AMMod::applySettings(const AMModSettings& settings, bool force)
{
    if ((settings.m_inputFrequencyOffset != m_settings.m_inputFrequencyOffset) ||
        (settings.m_outputSampleRate != m_settings.m_outputSampleRate) || force)
    {
        m_settingsMutex.lock();
        m_carrierNco.setFreq(settings.m_inputFrequencyOffset, settings.m_outputSampleRate);
        m_settingsMutex.unlock();
    }

    if((settings.m_outputSampleRate != m_settings.m_outputSampleRate) ||
       (settings.m_rfBandwidth != m_settings.m_rfBandwidth) ||
       (settings.m_audioSampleRate != m_settings.m_audioSampleRate) || force)
    {
        m_settingsMutex.lock();
        m_interpolatorDistanceRemain = 0;
        m_interpolatorConsumed = false;
        m_interpolatorDistance = (Real) settings.m_audioSampleRate / (Real) settings.m_outputSampleRate;
        m_interpolator.create(48, settings.m_audioSampleRate, settings.m_rfBandwidth / 2.2, 3.0);
        m_settingsMutex.unlock();
    }

    if ((settings.m_toneFrequency != m_settings.m_toneFrequency) ||
        (settings.m_audioSampleRate != m_settings.m_audioSampleRate) || force)
    {
        m_settingsMutex.lock();
        m_toneNco.setFreq(settings.m_toneFrequency, settings.m_audioSampleRate);
        m_settingsMutex.unlock();
    }

    if ((settings.m_audioSampleRate != m_settings.m_audioSampleRate) || force)
    {
        m_cwKeyer.setSampleRate(settings.m_audioSampleRate);
    }

    m_settings = settings;
}
