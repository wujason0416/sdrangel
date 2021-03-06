/**
 * SDRangel
 * This is the web API of SDRangel SDR software. SDRangel is an Open Source Qt5/OpenGL 3.0+ GUI and server Software Defined Radio and signal analyzer in software. It supports Airspy, BladeRF, HackRF, LimeSDR, PlutoSDR, RTL-SDR, SDRplay RSP1 and FunCube
 *
 * OpenAPI spec version: 4.0.0
 * Contact: f4exb06@gmail.com
 *
 * NOTE: This class is auto generated by the swagger code generator program.
 * https://github.com/swagger-api/swagger-codegen.git
 * Do not edit the class manually.
 */

/*
 * SWGPresetTransfer.h
 * 
 * Preset transfer to or from a device set
 */

#ifndef SWGPresetTransfer_H_
#define SWGPresetTransfer_H_

#include <QJsonObject>


#include "SWGPresetIdentifier.h"

#include "SWGObject.h"


namespace SWGSDRangel {

class SWGPresetTransfer: public SWGObject {
public:
    SWGPresetTransfer();
    SWGPresetTransfer(QString* json);
    virtual ~SWGPresetTransfer();
    void init();
    void cleanup();

    QString asJson ();
    QJsonObject* asJsonObject();
    void fromJsonObject(QJsonObject &json);
    SWGPresetTransfer* fromJson(QString &jsonString);

    qint32 getDeviceSetIndex();
    void setDeviceSetIndex(qint32 device_set_index);

    SWGPresetIdentifier* getPreset();
    void setPreset(SWGPresetIdentifier* preset);


private:
    qint32 device_set_index;
    SWGPresetIdentifier* preset;
};

}

#endif /* SWGPresetTransfer_H_ */
