/*!
 * @file Ogn.cpp
 *
 *
 */

#include "Ogn.h"

Ogn::Ogn(){
    _user = "";
}

bool Ogn::begin(String user,String version){
    _version = version;
    _user = "FNB" + user; //base-station
    connected = false;
    return true;
}

void Ogn::end(void){

}

void Ogn::sendLoginMsg(void){
    //String login ="user " + _user + " pass " + calcPass(_user) + " vers " + _version + " filter m/10\r\n";
    String login ="user " + _user + " pass " + calcPass(_user) + " vers " + _version + "\r\n";
    //log_i("%s",login.c_str());
    client.print(login);
}

String Ogn::calcPass(String user){
    const int length = user.length();
    uint8_t buffer[length];
    String myString = user.substring(0,10);
    myString.toUpperCase();
    //log_i("user=%s",myString.c_str());
    memcpy(buffer, myString.c_str(), length);
    uint16_t hash = 0x73e2;
    int i = 0;
    //log_i("i=%d hash=%02X",i,hash);
    while (i < length){
        hash ^= buffer[i] << 8;
        //log_i("i=%d hash=%02X",i,hash);
        if ((i+1) < length){
            hash ^= buffer[i+1];
            //log_i("i=%d hash=%02X",i,hash);
        }else{

        }
        i += 2;
    }
    hash &= 0x7fff;
    //log_i("hash=%02X",i,hash);
    //log_i("hash=%d",i,hash);
    return String(hash);
}

void Ogn::connect2Server(void){
    initOk = 0;
    //sntp_setoperatingmode(SNTP_OPMODE_POLL);
    //sntp_setservername(0, "pool.ntp.org");
    //sntp_init();
    //init and get the time
    configTime(0, 0, "pool.ntp.org");
    if (client.connect("aprs.glidernet.org", 14580)) {
        sendLoginMsg();
        connected = true;
    }
}

void Ogn::checkLine(String line){
    String s = "";
    int32_t pos = 0;
    //log_i("line=%s",line.c_str());
    if (initOk == 0){
        pos = getStringValue(line,"server ","\r\n",0,&s);
        if (pos > 0){
            initOk = 1;
            tStatus = millis() - OGNSTATUSINTERVALL;
            tRecBaecon = millis() - OGNSTATUSINTERVALL;
            _servername = s;
            //log_i("servername=%s",_servername.c_str());
        }
        //_servername = s;
        
    }
}

void Ogn::setGPS(float lat,float lon,float alt,float speed,float heading){
    _lat = lat;
    _lon = lon;
    _alt = alt;
    _speed = speed;
    _heading = heading;
}

uint8_t Ogn::getSenderDetails(aircraft_t aircraftType,String devId){
    uint8_t type = 0;
    switch (aircraftType)
    {
    case paraglider:
        type = 7;
        break;
    case hangglider:
        type = 6;
        break;
    case balloon:
        type = 11;
        break;
    case glider:
        type = 1;
        break;
    case poweredAircraft:
        type = 8;
        break;
    case helicopter:
        type = 3;
        break;
    case uav:
        type = 13;
        break;
    }
    type = type << 2;
    if (!devId.startsWith("11")){
        type += 3;
    }else{
        type += 2; //address type FLARM
    }
    
    return type;
}

void Ogn::sendTrackingData(float lat,float lon,float alt,float speed,float heading,float climb,String devId,aircraft_t aircraftType){
    char buff[200];
    int latDeg = int(lat);
    int latMin = (roundf((lat - int(lat)) * 60 * 1000));
    int lonDeg = int(lon);
    int lonMin = (roundf((lon - int(lon)) * 60 * 1000));
    String sTime = getActTimeString();
    if (sTime.length() <= 0) return;

    sprintf (buff,"FLR%s>APRS,TCPIP*,qAS,%s:/%sh%02d%02d.%02dN/%03d%02d.%02dEg%03d/%03d/A=%06d !W%01d%01d! id%02X%s %+03.ffpm\r\n"
                ,devId.c_str(),_servername.c_str(),sTime.c_str(),latDeg,latMin/1000,latMin/10 %100,lonDeg,lonMin/1000,lonMin/10 %100,int(heading),int(speed * 0.53996),int(alt * 3.28084),int(latMin %10),int(latMin %10),getSenderDetails(aircraftType,devId),devId.c_str(),climb*196.85f);
    client.print(buff);                
    //log_i("%s",buff);
}

void Ogn::sendReceiverStatus(String sTime){
    String sStatus = _user + ">APRS,TCPIP*,qAS," + _servername + ":>" + sTime + "h h00 v1.0.0.GX\r\n";
    client.print(sStatus);
    //log_i("%s",sStatus.c_str());
}

void Ogn::sendReceiverBeacon(String sTime){
    char buff[200];
    int latDeg = int(_lat);
    int latMin = (roundf((_lat - int(_lat)) * 60 * 1000));
    int lonDeg = int(_lon);
    int lonMin = (roundf((_lon - int(_lon)) * 60 * 1000));

    sprintf (buff,"%s>APRS,TCPIP*,qAS,%s:/%sh%02d%02d.%02dNI%03d%02d.%02dE&%03d/%03d/A=%06d !W%01d%01d!\r\n"
                ,_user.c_str(),_servername.c_str(),sTime.c_str(),latDeg,latMin/1000,latMin/10 %100,lonDeg,lonMin/1000,lonMin/10 %100,int(_heading),int(_speed * 0.53996),int(_alt * 3.28084),int(latMin %10),int(latMin %10));
    client.print(buff);                
    //log_i("%s",buff);
}

String Ogn::getActTimeString(){
    struct tm timeinfo;
    char buff[20];
    if(getLocalTime(&timeinfo)){
        strftime(buff, 20, "%H%M%S", &timeinfo);
        return String(buff);
    }else{
        return "";
    }
}

void Ogn::sendStatus(uint32_t tAct){
    if (initOk == 1){
        
        if ((tAct - tStatus) >= OGNSTATUSINTERVALL){
            tStatus = tAct;
            String sTime = getActTimeString();
            if (sTime.length() > 0){
                sendReceiverStatus(sTime);
            }
        }
        if ((_lat != NAN) && (_lon != NAN)){
            if ((tAct - tRecBaecon) >= OGNSTATUSINTERVALL){
                tRecBaecon = tAct;
                String sTime = getActTimeString();
                if (sTime.length() > 0){
                    sendReceiverBeacon(sTime);
                }
            }
        }
    }
}

void Ogn::readClient(){
  String line = "";
  while (client.available()){
    char c = client.read(); //read 1 character 
    
    line += c; //read 1 character
    if (c == '\n'){
        checkLine(line);
        line = "";
    }

  }
}

void Ogn::run(void){
    uint32_t tAct = millis();
    if (WiFi.status() == WL_CONNECTED){
        if (!connected) connect2Server();
        readClient();
        if (connected){
            sendStatus(tAct);
        }
    }else{
        if (connected) client.stop();
        connected = false;
    }
}