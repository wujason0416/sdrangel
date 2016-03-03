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

#ifndef INCLUDE_SDRDAEMONGUI_H
#define INCLUDE_SDRDAEMONGUI_H

#include <QTimer>
#include <sys/time.h>
#include "plugin/plugingui.h"

#include "sdrdaemoninput.h"

class PluginAPI;

namespace Ui {
	class SDRdaemonGui;
}

class SDRdaemonGui : public QWidget, public PluginGUI {
	Q_OBJECT

public:
	explicit SDRdaemonGui(PluginAPI* pluginAPI, QWidget* parent = NULL);
	virtual ~SDRdaemonGui();
	void destroy();

	void setName(const QString& name);
	QString getName() const;

	void resetToDefaults();
	QByteArray serialize() const;
	bool deserialize(const QByteArray& data);
	virtual qint64 getCenterFrequency() const;
	virtual void setCenterFrequency(qint64 centerFrequency);
	virtual bool handleMessage(const Message& message);

private:
	Ui::SDRdaemonGui* ui;

	PluginAPI* m_pluginAPI;
	QTimer m_updateTimer;
	SampleSource* m_sampleSource;
    bool m_acquisition;

	int m_sampleRate;
	int m_sampleRateStream;
	quint64 m_centerFrequency;
	struct timeval m_startingTimeStamp;
	bool m_syncLocked;
	uint32_t m_frameSize;
	bool m_lz4;
	float m_compressionRatio;
	uint32_t m_nbLz4DataCRCOK;
	uint32_t m_nbLz4SuccessfulDecodes;

	int m_samplesCount;
	std::size_t m_tickCount;

	QString m_address;
	quint16 m_port;
	bool m_dcBlock;
	bool m_iqCorrection;

	void displaySettings();
	void displayTime();
	void configureUDPLink();
	void configureAutoCorrections();
	void updateWithAcquisition();
	void updateWithStreamData();
	void updateWithStreamTime();

private slots:
	void handleSourceMessages();
	void on_applyButton_clicked(bool checked);
	void on_dcOffset_toggled(bool checked);
	void on_iqImbalance_toggled(bool checked);
	void on_address_textEdited(const QString& arg1);
	void on_port_textEdited(const QString& arg1);
	void tick();
};

#endif // INCLUDE_SDRDAEMONGUI_H