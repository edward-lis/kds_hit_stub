/*
 * U=(codeADC-codeOffset)*k, k=Urefer/codeADCrefer
 * Urefer=35.0 V, codeADCrefer=0xFFFF
 * codeOffset=0x0000, for example
 *
 * Ulimit_ocg=32.3v, codeADClimit_ocg=Ulimit_ocg/Urefer*codeADCrefer+codeOffset=32.3/35.0*0xffff+0x0000
 * float v=31.3+4*my_rand(2) - прибавить к базовому числу случайное от 0 до 3.99
 *
 */


//#include <QTextStream>
#include <QCoreApplication>
#include <QtSerialPort/QSerialPortInfo>
#include <QtSerialPort/QSerialPort>
#include <QDebug>
#include <QTime>
#include <QTextCodec>
#include <QFileInfo>
#include <QSettings>
#include "qmath.h"

QT_USE_NAMESPACE

void answer(QByteArray);
void answer(QByteArray, quint16);
void send_request(quint8, QByteArray, quint8);

float fUrefer1=8.51;
quint16 codeADCrefer1=0x3980;
float coefADC1=fUrefer1/codeADCrefer1; ///< коэффициент пересчёта кода АЦП в вольты
float fUrefer2=35.0;
quint16 codeADCrefer2=0xFFFF;
quint16 codeOffset1=0x0000;
quint16 codeOffset2=0x0000;
float fUlimit_case=1.0; // предел напряжения на корпусе
float fUlimit_ocg=32.3; // предел напряжения разомкнутой цепи группы и батареи
float fUlimit_ccg=27.0; // предел напряжения замкнутой цепи группы
float fUlimit_ccb=30.0; // предел напряжения замкнутой цепи батареи
float fUlimit_ocpb=6.9; // предел напряжения разомкнутой цепи БП УУТББ
float fUlimit_ccpb=5.7; // предел напряжения замкнутой цепи БП УУТББ
float fU; //тестовое случайное напряжение
quint16 codeUocg; // тестовый случайный код по напряжению fU
quint16 codeUccg; // тестовый случайный код по напряжению fU


QSerialPort serialPort;
QByteArray ba; //received data

QByteArray answer_pfx; //answer_pfx.resize(2); answer_pfx[0]=0xAA; answer_pfx[1]=0xAA; // префикс ответа
qint64 bw = 0; //bytes really writed
unsigned char inBuf[256]={0};
int i=0, inLen = 0;
quint8 prfx1=0, prfx2=0, operation_code=0, length=0, nmc=0x07,//номер устройства - 07 для БИП, 09 для Имитатора батареи
        crc = 0;
QTextCodec *codec;
QByteArray text;

void delay( int millisecondsToWait )
{
    QTime dieTime = QTime::currentTime().addMSecs( millisecondsToWait );
    while( QTime::currentTime() < dieTime )
    {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 100 );
    }
}

//случайные числа 0...1, accuracy - точность, кол-во знаков после запятой.
double my_rand(int accuracy)
{
  double a = 0;
  a = (qrand() % int (qPow(10, accuracy) + 1))/qPow(10, accuracy);
  return a;
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QTextStream out(stdout);
    QList<QSerialPortInfo> serialPortInfoList = QSerialPortInfo::availablePorts();

    int argumentCount = QCoreApplication::arguments().size();
    QStringList argumentList = QCoreApplication::arguments();

    QTextStream standardOutput(stdout);

    // инициализация генератора сл.чисел
    QTime midnight(0,0,0);
    qsrand(midnight.secsTo(QTime::currentTime()));

    codec = QTextCodec::codecForName("Windows-1251"); // для строк, которые будут передаваться/приниматься в буфере обмена
    answer_pfx.resize(2); answer_pfx[0]=0xAA; answer_pfx[1]=0xAA; // префикс ответа

    //=== Список доступных портов, и открытие порта
    out << QObject::tr("Total number of ports available: ") << serialPortInfoList.count() << endl;

    const QString blankString = QObject::tr("N/A");
    QString description;
    QString manufacturer;
    QString serialNumber;

    foreach (const QSerialPortInfo &serialPortInfo, serialPortInfoList) {
        description = serialPortInfo.description();
        manufacturer = serialPortInfo.manufacturer();
        serialNumber = serialPortInfo.serialNumber();
        out << endl
            << QObject::tr("Port: ") << serialPortInfo.portName() << endl
            << QObject::tr("Location: ") << serialPortInfo.systemLocation() << endl
            << QObject::tr("Description: ") << (!description.isEmpty() ? description : blankString) << endl
            << QObject::tr("Manufacturer: ") << (!manufacturer.isEmpty() ? manufacturer : blankString) << endl
            << QObject::tr("Serial number: ") << (!serialNumber.isEmpty() ? serialNumber : blankString) << endl
            << QObject::tr("Vendor Identifier: ") << (serialPortInfo.hasVendorIdentifier() ? QByteArray::number(serialPortInfo.vendorIdentifier(), 16) : blankString) << endl
            << QObject::tr("Product Identifier: ") << (serialPortInfo.hasProductIdentifier() ? QByteArray::number(serialPortInfo.productIdentifier(), 16) : blankString) << endl
            << QObject::tr("Busy: ") << (serialPortInfo.isBusy() ? QObject::tr("Yes") : QObject::tr("No")) << endl;
    }

    if (argumentCount == 1) {
        standardOutput << endl;
        //standardOutput << QObject::tr("Usage: %1 <serialportname> [baudrate]").qPrintable( QFileInfo(QCoreApplication::applicationFilePath()).fileName()) << endl;
        standardOutput << QObject::tr("Usage: %1 <serialportname> [baudrate]").arg(argumentList.first()) << endl;
        return 1;
    }

    QString serialPortName = argumentList.at(1);
    serialPort.setPortName(serialPortName);

    int serialPortBaudRate = (argumentCount > 2) ? argumentList.at(2).toInt() : QSerialPort::Baud115200;
    //serialPort.setBaudRate(serialPortBaudRate);

    if (!serialPort.open(QIODevice::ReadWrite /*| QIODevice::Unbuffered*/ )) {
        standardOutput << QObject::tr("Failed to open port %1, error: %2").arg(serialPortName).arg(serialPort.errorString()) << endl;
        return 1;
    }

    qDebug() << "\nSerial device " << serialPort.portName() << " open in " << serialPort.openMode();

    //Here, the default current parameters (for example)
    qDebug() << "= Default parameters =";
    qDebug() << "Device name            : " << serialPort.portName();
    qDebug() << "Baud rate              : " << serialPort.baudRate();
    qDebug() << "Data bits              : " << serialPort.dataBits();
    qDebug() << "Parity                 : " << serialPort.parity();
    qDebug() << "Stop bits              : " << serialPort.stopBits();
    qDebug() << "Flow                   : " << serialPort.flowControl();
    //qDebug() << "Total read timeout constant, msec : " << serialPort.totalReadConstantTimeout();
    //qDebug() << "Char interval timeout, usec       : " << serialPort.charIntervalTimeout();

    serialPort.setBaudRate(serialPortBaudRate);
    serialPort.setDataBits(QSerialPort::Data8);
    serialPort.setParity(QSerialPort::NoParity);
    serialPort.setStopBits(QSerialPort::OneStop);
    serialPort.setFlowControl(QSerialPort::NoFlowControl);

    //Here, the new set parameters (for example)
    qDebug() << "= New parameters =";
    qDebug() << "Device name            : " << serialPort.portName();
    qDebug() << "Baud rate              : " << serialPort.baudRate();
    qDebug() << "Data bits              : " << serialPort.dataBits();
    qDebug() << "Parity                 : " << serialPort.parity();
    qDebug() << "Stop bits              : " << serialPort.stopBits();
    qDebug() << "Flow                   : " << serialPort.flowControl();


    int rrto = 0; //timeout for ready read
//        cout << "Please enter wait timeout for ready read, msec : ";
//        cin >> rrto;
    rrto = 10000; // 10 sec

    int len = 0; //len data for read
//        cout << "Please enter len data for read, bytes : ";
//        cin >> len;
    len = 8; // 8 bytes minimum

    qDebug() << "Enter is " << rrto << " msecs, " << len << " bytes";
    qDebug() << "Starting waiting ready read in time : " << QTime::currentTime();

    //-----test print request commands 9ER20PSB-12
    //send_request(0x08, "", 0x00); send_request(0x08, "", 0x07); send_request(0x08, "", 0x09);
/*        send_request(0x01, "PING", 0x00); send_request(0x01, "PING", 0x07); send_request(0x01, "PING", 0x09);
    send_request(0x08, "BAT 9ER20PSB-12", 0x00); send_request(0x08, "BAT 9ER20PSB-12", 0x07); send_request(0x08, "BAT 9ER20PSB-12", 0x09);
    send_request(0x08, "BAT IMITATOR", 0x00); send_request(0x08, "BAT IMITATOR", 0x07); send_request(0x08, "BAT IMITATOR", 0x09);
    send_request(0x08, "BAT IDLE", 0x00); send_request(0x08, "BAT IDLE", 0x07); send_request(0x08, "BAT IDLE", 0x09);
    send_request(0x08, "BAT UNKNOWN", 0x00); send_request(0x08, "BAT UNKNOWN", 0x07); send_request(0x08, "BAT UNKNOWN", 0x09);
    send_request(0x08, "BAT ?", 0x00); send_request(0x08, "BAT ?", 0x07); send_request(0x08, "BAT ?", 0x09);
    send_request(0x08, "Rins V1+", 0x00); send_request(0x08, "Rins V1+", 0x07); send_request(0x08, "Rins V1+", 0x09);
    send_request(0x08, "Rins ?", 0x00); send_request(0x08, "Rins ?", 0x07); send_request(0x08, "Rins ?", 0x09);
    send_request(0x08, "Uoc N01", 0x00); send_request(0x08, "Uoc N01", 0x07); send_request(0x08, "Uoc N01", 0x09);
    send_request(0x08, "Uoc ?", 0x00); send_request(0x08, "Uoc ?", 0x07); send_request(0x08, "Uoc ?", 0x09);
    send_request(0x08, "Ucc N01 I0.1", 0x00); send_request(0x08, "Ucc N01 I0.1", 0x07); send_request(0x08, "Ucc N01 I0.1", 0x09);
    send_request(0x08, "Ucc ?", 0x00); send_request(0x08, "Ucc ?", 0x07); send_request(0x08, "Ucc ?", 0x09);
*/        /*send_request(0x08, "", 0x00); send_request(0x08, "", 0x07); send_request(0x08, "", 0x09);
    send_request(0x08, "", 0x00); send_request(0x08, "", 0x07); send_request(0x08, "", 0x09);
    send_request(0x08, "", 0x00); send_request(0x08, "", 0x07); send_request(0x08, "", 0x09);
    send_request(0x08, "", 0x00); send_request(0x08, "", 0x07); send_request(0x08, "", 0x09);
    send_request(0x08, "", 0x00); send_request(0x08, "", 0x07); send_request(0x08, "", 0x09);
    send_request(0x08, "", 0x00); send_request(0x08, "", 0x07); send_request(0x08, "", 0x09);
    send_request(0x08, "", 0x00); send_request(0x08, "", 0x07); send_request(0x08, "", 0x09);
    send_request(0x08, "", 0x00); send_request(0x08, "", 0x07); send_request(0x08, "", 0x09);*/

    /* 5. Fifth - you can now read / write device, or further modify its settings, etc.
    */
    while(1)
    {
        // read
begin:      ba.clear(); // очистим приёмный буффер
        prfx1 = 0;
        prfx2 = 0;
        operation_code = 0;
        length = 0;
        crc = 0;

        // приём префикса
        // читаем по байту, ждём префикса
        while(1)
        {
            // если есть данные в порту, и не случился таймаут
            if ((serialPort.bytesAvailable() > 0) ||  serialPort.waitForReadyRead(rrto))
            {
                // читаем один байт
                ba += serialPort.read(1);
                prfx1 = ba[ba.size()-1];
                //qDebug() << "Read prefix byte is : " << ba.size() << " bytes " << ba.toHex();
                //qDebug() << "Read prefix byte is : " << prfx1;
                //printf("Read prefix byte is : %c\n", prfx1);
                // если текущий байт равен префиксу
                if(prfx1 == 0xFF)
                {
                    // и предыдущий байт тоже равен префиксу, то вываливаемся из цикла приёма префикса
                    if(prfx2 == 0xff) break;
                }
                prfx2 = prfx1;
            }
            // если таймаут, то начинаем сначала
            else
            {
                qDebug() << "Timeout read prefix in time : " << QTime::currentTime();
                goto begin;
                continue;
            }
        }
        // приняли префикс, теперь принимаем код операции
        if ((serialPort.bytesAvailable() > 0) ||  serialPort.waitForReadyRead(rrto))
        {
            // читаем один байт.   начнём заполнять буфер сначала (чтобы потом посчитать crc), поэтому не +=,а просто =
            ba = serialPort.read(1);
            operation_code = ba[ba.size()-1];
            //crc += operation_code;
            qDebug() << "Read operation code byte is : " << operation_code;
        }
        // если таймаут, то начинаем сначала
        else
        {
            qDebug() << "Timeout read operation code in time : " << QTime::currentTime();
            goto begin;
            continue;
        }
        // приняли код операции, теперь принимаем длину посылки - кол-во оставшихся байт, включая crc
        if ((serialPort.bytesAvailable() > 0) ||  serialPort.waitForReadyRead(rrto))
        {
            // читаем один байт.
            ba += serialPort.read(1);
            length = ba[ba.size()-1];
            //crc += length;
            qDebug() << "Read lenght byte is : " << length;
        }
        // если таймаут, то начинаем сначала
        else
        {
            qDebug() << "Timeout read lenght in time : " << QTime::currentTime();
            goto begin;
            continue;
        }
        //
        qDebug() << "ba size " << ba.size() << " :" << ba.toHex();
        // приняли длину оставшейся посылки, теперь принимаем саму посылку
        if ((serialPort.bytesAvailable() > 0) ||  serialPort.waitForReadyRead(rrto))
        {
            // читаем length байт
            /*это если весь запрашиваемый пакет примится за раз.
            ba += serialPort.read(length);
            qDebug() << "Read body bytes is : " << " lenght " << length << " read size " << ba.size() << " buffer: " << ba.toHex();
            qDebug() << "buf: " << ba;
            // проверить соответствие запрашиваемой длины и принятой длины. если не равно, то сначала.
            if((ba.size()-2) != length)
            {
                qDebug() << "Wrong lenght! break.";
                goto begin;
                continue;
            }*/
            int rest=length;
            do
            {
                if ((serialPort.bytesAvailable() > 0) ||  serialPort.waitForReadyRead(rrto))
                {
                ba += serialPort.read(rest);
                //qDebug() << "Read body bytes is : " << " lenght " << length << " read size " << ba.size() << " buffer: " << ba.toHex();
                //qDebug() << "buf: " << qPrintable(ba);
                rest=length - (ba.size() - 2);
                //qDebug()<<"length" << length<< " ba.size() "<<ba.size()<<" rest "<<rest;
                }
                else
                {
                    qDebug() << "Timeout read rest body in time : " << QTime::currentTime();
                    goto begin;
                    continue;
                    rest=0;
                }
            }while(rest);
            //qDebug() << "Read body bytes is : " << ba.toHex();
        }
        // если таймаут, то начинаем сначала
        else
        {
            qDebug() << "Timeout read left body in time : " << QTime::currentTime();
            goto begin;
            continue;
        }
        // пробежаться по буферу, посчитать crc
        for(i=0; i<ba.size()-1; i++)
        {
            crc += ba[i];
        }
        if(crc != (quint8)ba[ba.size()-1])
        {
            qDebug() << "Wrong CRC! Received crc ba.[" << ba.size()-1 << "]=" << QString::number((quint8)ba[ba.size()-1],16) << " but count crc is 0x" << QString::number(crc,16);
            goto begin;
            continue;
        }
        else qDebug() << "Good CRC = " << crc;
        nmc = ba[ba.size()-2]; // номер устройства
        // если пакет принят нормально, начинаем его разбирать
        qDebug()<<"\n";
//==================================================================================================================================
        // PING
        if(operation_code == 0x01)
        {
            // ответ пинг, только префикс добавим
            ba.insert(0,answer_pfx); // всунули вначало префикс ответного пакета
            bw = serialPort.write(ba); // послали ответ
            qDebug() << "Writed is : " << bw << " bytes";
        }
        // команды
        if(operation_code == 0x08)
        {
            ba.remove(0,2);// вырезать первые 2 байта (длина и код операции)
            ba.chop(2); // отрезать с конца буфера два байта (nmc и crc)
            text = codec->fromUnicode("НЕКИЙ ТЕКСТ"); // текст перекодируется из юникода(кодировка исходника) в Windows-1251 (кодировка протокола передачи данных)
            if(ba.indexOf(text,2) == 2) // ищем текст в буфере, начиная со второго байта. если текст начинается со второго байта (ф-ия возвращает 2), то хорошо
            {
            }
            //======= СТОП Режима
            text = codec->fromUnicode("IDLE#");
            if(ba.contains(text)) // выбран режим сброса
            {
               qDebug() << "IDLE";
               delay(100); // сымитируем задержечку на время измерений
               // ответ посылкой 0xAA;0xAA;0x08;9;"IDLE#OK";7;CRC;
               QString tmp=QString("IDLE#OK")+"1"+'\r'+'\n'; // номер комплекта плат 0...3.   \n - просто для удобства вывода на экран для д.Гриши.
               answer(qPrintable(tmp));
               qDebug() << QString("ANSWER")+"IDLE#OK"+"0\n";
            }
            //======= Определение полярности батареи
            text = codec->fromUnicode("Polar#");
            if(ba.contains(text)) // выбран режим полярности батареи
            {
               qDebug() << "Polar";
               delay(100); // сымитируем задержечку на время измерений
               answer("Polar#OK");
            }
            text = codec->fromUnicode("Polar?#");
            if(ba.contains(text)) // выбран режим запрос полярности батареи
            {
               qDebug() << "Polar?";
               delay(100); // сымитируем задержечку на время измерений
               // ответ: 0(прямая: 9ER20P-20 или 9ER20P-28);1(обратная: 9ER14P-24 или 9ЕR14PS-24;)
               answer("   #Polar OK", 1); // 0,1
            }

            text = codec->fromUnicode("TypeB 28#");
            if(ba.contains(text)) // выбран режим полярности батареи
            {
               qDebug() << "TypeB 28";
               delay(100); // сымитируем задержечку на время измерений
               answer("TypeB 28#OK");
            }
            text = codec->fromUnicode("TypeB?#");
            if(ba.contains(text)) // выбран режим запрос полярности батареи
            {
               qDebug() << "TypeB?";
               delay(100); // сымитируем задержечку на время измерений
               fU=fUlimit_ocg-1 + 3*my_rand(2); // от нижнего предела отнимем вольт, и прибавим случайное в пределах 3 вольт
               codeUocg=fU/fUrefer1*codeADCrefer1+codeOffset1;
               qDebug()<<"fU"<<fU<<"codeUocg"<<codeUocg;
               // ответ: напряжение в кодах
               answer("   #TypeB 28OK", codeUocg); //0, 43243 ==P-28 код напряжения = 25,0В/(k=8,51В/14720(=0x3980))
            }

            //======================Проверка НРЦ БП УУТББ
            text = codec->fromUnicode("UocPB#");
            if(ba.contains(text)) // выбран режим напряжения разомкнутой цепи БП УУТББ
            {
               qDebug() << "UocPB";
               delay(100); // сымитируем задержечку на время измерений
               answer(ba+"OK");
            }
            text = codec->fromUnicode("UocPB?#");
            if(ba.contains(text)) // выбран режим запрос напряжения разомкнутой цепи БП УУТББ
            {
                fU=fUlimit_ocpb-0.2 + 0.3*my_rand(2);
                codeUocg=fU/fUrefer1*codeADCrefer1+codeOffset1;
                qDebug()<<"fU"<<fU<<"codeUocg"<<showbase<<uppercasedigits<<hex<<codeUocg;
                qDebug() << "UocPB?";
                delay(100); // сымитируем задержечку на время измерений
                // ответ: напряжение в сотых
                answer("   #UocPB OK", codeUocg);
            }
            //================напряжение на корпусе========
            fU=fUlimit_case-1 + 1.5*my_rand(2); // от нижнего предела отнимем вольт, и прибавим случайное в пределах 1.5 вольт
            qint16 codeUcase =fU/fUrefer2*codeADCrefer2+codeOffset2;

            text = codec->fromUnicode("UcaseP#");
            if(ba.contains(text)) // выбран режим напряжения на корпусе
            {
               qDebug() << "UcaseP";
               delay(100); // сымитируем задержечку на время измерений
               answer("UcaseP#OK");
            }
            text = codec->fromUnicode("UcaseP?#");
            if(ba.contains(text)) // выбран режим запрос напряжения на корпусе
            {
               qDebug() << "UcaseP?";
               delay(100); // сымитируем задержечку на время измерений
               // ответ: напряжение кодах АЦП
               qDebug()<<"fU"<<fU<<"codeUcase"<<codeUcase;
               answer("   #UcasePOK", codeUcase); // 0, 1872 - код напряжения = 1,0В/(k=35.0В/65535(=0xFFFF))
            }
            text = codec->fromUnicode("UcaseM#");
            if(ba.contains(text)) // выбран режим напряжения на корпусе
            {
               qDebug() << "UcaseM";
               delay(100); // сымитируем задержечку на время измерений
               answer("UcaseM#OK");
            }
            text = codec->fromUnicode("UcaseM?#");
            if(ba.contains(text)) // выбран режим запрос напряжения на корпусе
            {
               qDebug() << "UcaseM?";
               delay(100); // сымитируем задержечку на время измерений
               // ответ: напряжение кодах АЦП
               qDebug()<<"fU"<<fU<<"codeUcase"<<codeUcase;
               answer("   #UcaseMOK", codeUcase); // 0, 1872 - код напряжения = 1,0В/(k=35.0В/65535(=0xFFFF))
            }
            //=======Проверка сопротивления изоляции
            text = codec->fromUnicode("Rins ");
            if(ba.contains(text))
            {
                qDebug() << "Start insulasion check"<<ba;
                delay(100); // сымитируем задержечку на время измерений
                answer(ba+"OK");
            }
            text = codec->fromUnicode("Rins?");
            if(ba.contains(text))
            {
                qDebug() << "Rins?";
                delay(100); // сымитируем задержечку на время измерений
                // ответ посылкой
                answer("   #Rins nnOK", 0xffff); //0x334); //0x0795); //0x099
            }
            //======================Проверка НРЦ групп

            text = codec->fromUnicode("UocG ");
            if(ba.contains(text)) // выбран режим напряжения разомкнутой цепи группы
            {
               qDebug() << "UocG";
               delay(100); // сымитируем задержечку на время измерений
               answer(ba+"OK");
            }
            text = codec->fromUnicode("UocG?#");
            if(ba.contains(text)) // выбран режим запрос напряжения разомкнутой цепи группы
            {
                fU=fUlimit_ocg-1 + 3*my_rand(2); // от нижнего предела отнимем вольт, и прибавим случайное в пределах 3 вольт
                codeUocg=fU/fUrefer1*codeADCrefer1+codeOffset1;
                qDebug()<<"fU"<<fU<<"codeUocg"<<codeUocg;
               qDebug() << "UocG?";
               delay(100); // сымитируем задержечку на время измерений
               // ответ: напряжение в сотых
               answer("   #UocG gg OK", codeUocg); // 0, 8648 - код напряжения = 5,0В/k
            }
            //======================Проверка НРЦ батареи
            text = codec->fromUnicode("UocB#");
            if(ba.contains(text)) // выбран режим напряжения разомкнутой цепи группы
            {
               qDebug() << "UocB";
               delay(100); // сымитируем задержечку на время измерений
               answer(ba+"OK");
            }
            text = codec->fromUnicode("UocB?#");
            if(ba.contains(text)) // выбран режим запрос напряжения разомкнутой цепи группы
            {
                fU=fUlimit_ocg-1 + 3*my_rand(2);
                codeUocg=fU/fUrefer1*codeADCrefer1+codeOffset1;
                qDebug()<<"fU"<<fU<<"codeUocg"<<showbase<<uppercasedigits<<hex<<codeUocg;
                qDebug() << "UocB?";
                delay(100); // сымитируем задержечку на время измерений
                // ответ: напряжение в сотых
                answer("   #UocB OK", codeUocg);
            }
            //======================Проверка НЗЦ групп

            text = codec->fromUnicode("UccG ");
            if(ba.contains(text)) // выбран режим напряжения разомкнутой цепи группы
            {
               qDebug() << "UccG";
               delay(100); // сымитируем задержечку на время измерений
               answer(ba+"OK");
            }
            text = codec->fromUnicode("UccG?#");
            if(ba.contains(text)) // выбран режим запрос напряжения разомкнутой цепи группы
            {
                fU=fUlimit_ccg-0.3 + 0.8*my_rand(2);
                codeUccg=fU/fUrefer1*codeADCrefer1+codeOffset1;
                qDebug()<<"fU"<<fU<<"codeUccg"<<codeUccg;
               qDebug() << "UccG?";
               delay(100); // сымитируем задержечку на время измерений
               // ответ: напряжение в сотых
               answer("   #UccG gg OK", codeUccg);
            }
            //======================Проведение расспассивации
            //======================Проверка НЗЦ батареи
            text = codec->fromUnicode("UccB#");
            if(ba.contains(text)) // выбран режим напряжения разомкнутой цепи группы
            {
               qDebug() << "UccB#";
               delay(100); // сымитируем задержечку на время измерений
               answer(ba+"OK");
            }
            text = codec->fromUnicode("UccB?#");
            if(ba.contains(text)) // выбран режим запрос напряжения разомкнутой цепи группы
            {
                fU=fUlimit_ccb-1 + 3*my_rand(2);
                codeUccg=fU/fUrefer1*codeADCrefer1+codeOffset1;
                qDebug()<<"fU"<<fU<<"codeUccg"<<showbase<<uppercasedigits<<hex<<codeUccg;
               qDebug() << "UccB?";
               delay(100); // сымитируем задержечку на время измерений
               // ответ: напряжение в сотых
               answer("   #UccB OK", codeUccg);
            }
            //======================Проверка НЗЦ БП УУТББ
            text = codec->fromUnicode("UccPB#");
            if(ba.contains(text)) // выбран режим напряжения замкнутой цепи БП УУТББ
            {
               qDebug() << "UccPB";
               delay(100); // сымитируем задержечку на время измерений
               answer(ba+"OK");
            }
            text = codec->fromUnicode("UccPB?#");
            if(ba.contains(text)) // выбран режим запрос напряжения замкнутой цепи БП УУТББ
            {
                fU=fUlimit_ccpb-0.2 + 0.4*my_rand(2);
                codeUocg=fU/coefADC1+codeOffset1;
                //qDebug()<<"fUlimit_ccpb"<<fUlimit_ccpb<<"fUlimit_ccpb-0.1"<<(fUlimit_ccpb-0.1)<<"1*my_rand(2)"<<(1*my_rand(2))<<"coefADC1"<<coefADC1<<"codeOffset1"<<codeOffset1;
                qDebug()<<"fU"<<fU<<"codeUocg"<<showbase<<uppercasedigits<<hex<<codeUocg;
                qDebug() << "UccPB?";
                delay(100); // сымитируем задержечку на время измерений
                // ответ: напряжение в сотых
                answer("   #UccPB OK", codeUocg);
            }
            //======================Проверка НЗЦ БП УУТББ (с контролем тока)
            text = codec->fromUnicode("UccPBI#");
            if(ba.contains(text)) // выбран режим напряжения замкнутой цепи БП УУТББ
            {
               qDebug() << "UccPBI";
               delay(100); // сымитируем задержечку на время измерений
               answer(ba+"OK");
            }
            text = codec->fromUnicode("UccPBI?#");
            if(ba.contains(text)) // выбран режим запрос напряжения замкнутой цепи БП УУТББ
            {
                fU=fUlimit_ccpb-0.2 + 0.4*my_rand(2);
                codeUocg=fU/coefADC1+codeOffset1;
                //qDebug()<<"fUlimit_ccpb"<<fUlimit_ccpb<<"fUlimit_ccpb-0.1"<<(fUlimit_ccpb-0.1)<<"1*my_rand(2)"<<(1*my_rand(2))<<"coefADC1"<<coefADC1<<"codeOffset1"<<codeOffset1;
                qDebug()<<"fU"<<fU<<"codeUocg"<<showbase<<uppercasedigits<<hex<<codeUocg;
                qDebug() << "UccPBI?";
                delay(100); // сымитируем задержечку на время измерений
                // ответ: напряжение в сотых
                answer("   #UccPBI OK", codeUocg);
            }
            //======= Имитация Техно.Заглушки Х6
            text = codec->fromUnicode("StubX6#");
            if(ba.contains(text)) // выбран режим полярности батареи
            {
               qDebug() << "StubX6";
               delay(100); // сымитируем задержечку на время измерений
               answer("StubX6#OK");
            }

            //qDebug() << text;
        }
        serialPort.clear(QSerialPort::Input);
    }
/*        while (1) {
        //write
        bw = 0;
        cout << "Please enter count bytes for wtitten : ";
        cin >> bw;

        qDebug() << "Starting writting " << bw << " bytes in time : " << QTime::currentTime();

        ba.clear();
        ba.resize(bw);

        while (bw--) //filling data array
            ba[(int)bw] = bw;

        bw = serialPort.write(ba);
        qDebug() << "Writed is : " << bw << " bytes";

        // read
        if ((serialPort.bytesAvailable() > 0) ||  serialPort.waitForReadyRead(rrto)) {
            ba.clear();
            ba = serialPort.read(len);
            qDebug() << "Read is : " << ba.size() << " bytes";
        }
        else {
            qDebug() << "Timeout read data in time : " << QTime::currentTime();
        }
    }//while */

    serialPort.close();
    qDebug() << "Serial device " << serialPort.portName() << " is closed";

    return a.exec();
}

void answer(QByteArray text)
{
    quint8 operation_code=0x08; // в нынешнем протоколе эти цифры всегда такие и не меняются
    quint8 nmc=7;
    QByteArray ba;
    int bw=0;
    qint8 crc=0;
    //text = codec->fromUnicode(text);
        ba.clear();
        ba+=operation_code;
        ba+='0'; // зарезервируем место под длину пакета
        ba+=codec->fromUnicode(text);;
        ba+=nmc;
        ba[1]=ba.size()-1; // длина пакета
        for(int i=0; i<ba.size(); i++) crc += ba[i];
        ba+=crc;
        ba.insert(0,answer_pfx); // всунули в начало префикс ответного пакета
        bw = serialPort.write(ba); // послали ответ
        qDebug() << "Writed is : " << bw << " bytes " << ba.toHex();//<<" "<<QString(ba);
}

void answer(QByteArray text, quint16 data)
{
    quint8 operation_code=0x08; // в нынешнем протоколе эти цифры всегда такие и не меняются
    quint8 nmc=7;
    QByteArray ba;
    int bw=0;
    qint8 crc=0;
    //text = codec->fromUnicode(text);
        ba.clear();
        ba+=operation_code;
        ba+='0'; // зарезервируем место под длину пакета
        ba+=(data>>8); //H
        ba+=((quint8)data & 0xFF); //L
        ba+=codec->fromUnicode(text);;
        ba+=nmc;
        ba[1]=ba.size()-1; // длина пакета
        for(int i=0; i<ba.size(); i++) crc += ba[i];
        ba+=crc;
        ba.insert(0,answer_pfx); // всунули в начало префикс ответного пакета
        bw = serialPort.write(ba); // послали ответ
        qDebug() << "Writed is : " << bw << " bytes " << ba.toHex();
}

void send_request(quint8 operation_code, QByteArray text, quint8 nmc)
{
    QByteArray request_pfx; request_pfx.resize(2); request_pfx[0]=0xFF; request_pfx[1]=0xFF; // префикс запроса
    QByteArray ba;
    //int bw=0;
    qint8 crc=0;
    //text = codec->fromUnicode(text);
        ba.clear();
        ba+=operation_code;
        ba+='0'; // зарезервируем место под длину пакета
        ba+=codec->fromUnicode(text);;
        ba+=nmc;
        ba[1]=ba.size()-1; // длина пакета
        for(int i=0; i<ba.size(); i++) crc += ba[i];
        ba+=crc;
        ba.insert(0,request_pfx); // всунули вначало префикс ответного пакета
        bw = serialPort.write(ba); // послали запрос
        //qDebug() << "Request is : " << bw << " bytes " << ba.toHex();
        qDebug() << text;
        qDebug() << ba.toHex() << "\n";

}
