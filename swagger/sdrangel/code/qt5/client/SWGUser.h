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
 * SWGUser.h
 * 
 * 
 */

#ifndef SWGUser_H_
#define SWGUser_H_

#include <QJsonObject>


#include <QString>

#include "SWGObject.h"


namespace SWGSDRangel {

class SWGUser: public SWGObject {
public:
    SWGUser();
    SWGUser(QString* json);
    virtual ~SWGUser();
    void init();
    void cleanup();

    QString asJson ();
    QJsonObject* asJsonObject();
    void fromJsonObject(QJsonObject &json);
    SWGUser* fromJson(QString &jsonString);

    qint32 getIndex();
    void setIndex(qint32 index);

    QString* getName();
    void setName(QString* name);


private:
    qint32 index;
    QString* name;
};

}

#endif /* SWGUser_H_ */
