#include <stdio.h>
#include <unistd.h>

#include <QReadLocker>
#include <QWriteLocker>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QDebug>

#include <zguilib/global_gui.h>

#include "discrec.h"

//bool untar(const QString &arc, const QString &path)
//{
//    QProcess tar;
//    tar.start(QString("/bin/tar -xf %1 --directory %2").arg(arc).arg(path));
//    tar.waitForStarted();
//    bool tarNormalFinished = tar.waitForFinished(5*60*1000);
//    return tarNormalFinished && tar.exitStatus()==QProcess::NormalExit && tar.exitCode()==0;
//}

////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////// DiscRec //////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

DiscRec::DiscRec()
    :QObject(),
      mXorrisoProcess(NULL),
      mXorrisoWorking(false)
{
    setCurrentOperation(OPER_NO);
    setObjectName("DiscRec");

    //mProfilesMap.insert(0x00, "is not present");
    mProfilesMap.insert(0x01, "Non-removable disk");
    mProfilesMap.insert(0x02, "Removable disk");
    mProfilesMap.insert(0x03, "MO erasable");
    mProfilesMap.insert(0x04, "Optical write once");
    mProfilesMap.insert(0x05, "AS-MO");
    mProfilesMap.insert(0x08, "CD-ROM");
    mProfilesMap.insert(0x09, "CD-R");
    mProfilesMap.insert(0x0a, "CD-RW");
    mProfilesMap.insert(0x10, "DVD-ROM");
    mProfilesMap.insert(0x11, "DVD-R sequential recording");
    mProfilesMap.insert(0x12, "DVD-RAM");
    mProfilesMap.insert(0x13, "DVD-RW restricted overwrite");
    mProfilesMap.insert(0x14, "DVD-RW sequential recording");
    mProfilesMap.insert(0x15, "DVD-R/DL sequential recording");
    mProfilesMap.insert(0x16, "DVD-R/DL layer jump recording");
    mProfilesMap.insert(0x1a, "DVD+RW");
    mProfilesMap.insert(0x1b, "DVD+R");
    mProfilesMap.insert(0x2a, "DVD+RW/DL");
    mProfilesMap.insert(0x2b, "DVD+R/DL");
    mProfilesMap.insert(0x40, "BD-ROM");
    mProfilesMap.insert(0x41, "BD-R sequential recording");
    mProfilesMap.insert(0x42, "BD-R random recording");
    mProfilesMap.insert(0x43, "BD-RE");
    mProfilesMap.insert(0x50, "HD-DVD-ROM");
    mProfilesMap.insert(0x51, "HD-DVD-R");
    mProfilesMap.insert(0x52, "HD-DVD-RAM");

    mDiscStatusMap.insert(BURN_DISC_BLANK, "is blank");
    mDiscStatusMap.insert(BURN_DISC_EMPTY, "is not present");
    mDiscStatusMap.insert(BURN_DISC_APPENDABLE, "is written , is appendable");
    mDiscStatusMap.insert(BURN_DISC_FULL, "is written , is closed");
    mDiscStatusMap.insert(BURN_DISC_UNSUITABLE, "is not recognizable");
}

DiscRec::~DiscRec()
{
    stopXorriso();
}

bool DiscRec::runXorriso(QStringList args)
{
    qDebug() << "runXorriso" << args;
    if(mXorrisoProcess)
    {
        mXorrisoProcess->close();
    }

    mXorrisoRetCode = 0;
    mXorrisoProcess = new QProcess();

    connect(mXorrisoProcess, SIGNAL(started()), this, SLOT(slotXorrisoStarted()));
    connect(mXorrisoProcess, SIGNAL(finished(int)), this, SLOT(slotXorrisoFinished(int)));
    connect(mXorrisoProcess, SIGNAL(readyReadStandardError()), this, SLOT(slotXorrisoReadStderr()));
    connect(mXorrisoProcess, SIGNAL(readyReadStandardOutput()), this, SLOT(slotXorrisoReadStdout()));

    mXorrisoProcess->start("xorriso", args);
    mXorrisoWorking = mXorrisoProcess->waitForStarted();

    return mXorrisoWorking;
}

void DiscRec::stopXorriso(int msecWaitTime)
{
    if (!mXorrisoProcess)
        return;
    if (mXorrisoProcess->state() != QProcess::NotRunning)
    {
        mXorrisoProcess->terminate();
        int t = 0;
        while (true)
        {
            if (mXorrisoProcess->state() == QProcess::NotRunning)
                break;
            usleep(250 * 1000);
            t += 250;
            if (t >= msecWaitTime) // киляем
            {
                mXorrisoProcess->kill();
                break;
            }
        }
    }
    mXorrisoProcess->waitForFinished();
    delete mXorrisoProcess;
    mXorrisoProcess = NULL;
}

void DiscRec::slotXorrisoStarted()
{
    mXorrisoWorking = true;

}

void DiscRec::slotXorrisoFinished(int code)
{
    qDebug() << "process exit" << code;

    disconnect(mXorrisoProcess, SIGNAL(started()), this, SLOT(slotXorrisoStarted()));
    disconnect(mXorrisoProcess, SIGNAL(finished(int)), this, SLOT(slotXorrisoFinished(int)));
    disconnect(mXorrisoProcess, SIGNAL(readyReadStandardError()), this, SLOT(slotXorrisoReadStderr()));
    disconnect(mXorrisoProcess, SIGNAL(readyReadStandardOutput()), this, SLOT(slotXorrisoReadStdout()));

    mXorrisoRetCode = code;
    mXorrisoWorking = false;
}

void DiscRec::slotXorrisoReadStderr()
{
    QString str = mXorrisoProcess->readAllStandardError();

//    qDebug() << "STDERR" << str;// << curOperation;

    QRegExp rx("([^ ^%]*)% done");
    int pos = rx.indexIn(str) + rx.matchedLength();
    if(pos > 0)
    {
        mPercent = (int)rx.cap(1).toFloat();
    }

    rx = QRegExp("UPDATE : ([^M]*) MB written");
    pos = rx.indexIn(str) + rx.matchedLength();
    if(pos > 0)
    {
        QString str = rx.cap(1);
        int done = 0;
        int from = 0;

        rx = QRegExp("([^o]*) of");
        pos = rx.indexIn(str) + rx.matchedLength();
        if(pos)
            done = rx.cap(1).toInt();

        rx = QRegExp("of ([^*]*)");
        pos = rx.indexIn(str) + rx.matchedLength();

        if(pos)
            from = rx.cap(1).toInt();

        mPercent = (done * 100) / from;
    }

   // if(currentOperation() == OPER_DISC_DRIVE_INFO)
    {
        QRegExp rx("Media current: ([^\n]*)\n");

        int pos = rx.indexIn(str) + rx.matchedLength();
        if(pos > 0)
            mDiscProfileStr = rx.cap(1);


        rx = QRegExp("Media status : ([^\n]*)\n");
        pos = rx.indexIn(str) + rx.matchedLength();
        if(pos > 0)
        {
            QString statusStr = rx.cap(1);
            QMapIterator<burn_disc_status, QString> i(mDiscStatusMap);
            while(i.hasNext())
            {
                i.next();
                if(statusStr.indexOf(i.value()) >= 0)
                    mDiscStatus = i.key();
            }
        }

        rx = QRegExp("Media summary: ([^\n]*)\n");
        pos = rx.indexIn(str) + rx.matchedLength();
        if(pos > 0)
        {
            QString summaryStr = rx.cap(1);
            rx = QRegExp("([^ ]*) free");
            pos = rx.indexIn(str) + rx.matchedLength();
            if(pos > 0)
            {
                QString freeStr = rx.cap(1);

                mDiskFreeSize = freeStr.mid(0, freeStr.size() - 1).toDouble();
                if(freeStr.right(1) == "m")
                    mDiskFreeSize *= (1024 * 1024);
                else if(freeStr.right(1) == "k")
                    mDiskFreeSize *= 1024;
                else if(freeStr.right(1) == "g")
                    mDiskFreeSize *= (1024 * 1024 * 1024);

            }
        }

    }
}

void DiscRec::slotXorrisoReadStdout()
{
    QString str = mXorrisoProcess->readAllStandardOutput();

//    qDebug() << "STDOUT" << str;

    if(currentOperation() == OPER_SCANNING_RECORDERS)
    {
        if(mScanDrives)
        {
            QRegExp rx("'([^']*)'");

            int pos = rx.indexIn(str) + rx.matchedLength();
            QString location = rx.cap(1);

            pos = rx.indexIn(str, pos) + rx.matchedLength();;
            QString vendor = rx.cap(1);

            pos = rx.indexIn(str, pos);
            QString product = rx.cap(1);

            struct burn_drive *drv = new struct burn_drive;
            struct burn_drive_info *driveInfo = new struct burn_drive_info;

            drv->driveDevName = location;
            driveInfo->drive = drv;

            mDrives.insert(mDrivesCount, driveInfo);
            mDrivesNamesMap.insert(mDrivesCount++, QString("%1 %2 (dev='%3')").arg(vendor).arg(product).arg(location));

        }
        else
        {
            struct burn_drive_info *driveInfo = mDrives[mCurDrive];

            QRegExp rx("vendor '([^']*)'");

            int pos = rx.indexIn(str) + rx.matchedLength();
            if(pos > 0)
                driveInfo->vendor = rx.cap(1);

            rx = QRegExp("product '([^']*)'");
            pos = rx.indexIn(str) + rx.matchedLength();
            if(pos > 0)
                driveInfo->product = rx.cap(1);

            rx = QRegExp("revision '([^']*)'");
            pos = rx.indexIn(str) + rx.matchedLength();
            if(pos > 0)
                driveInfo->revision = rx.cap(1);

            rx = QRegExp("-outdev '([^']*)'");
            pos = rx.indexIn(str) + rx.matchedLength();
            if(pos > 0)
                driveInfo->location = rx.cap(1);

            pos = 0;
            while((pos = str.indexOf(": 0x", pos)) > 0)
            {
                int profile = str.mid(pos + 2, 6).toInt(NULL, 16);

                switch(profile)
                {
                case 0x12:
                    driveInfo->dvdRAM = true;
                    break;

                case 0x11:
                case 0x15:
                case 0x16:
                    driveInfo->dvdRMinus = true;
                    break;

                case 0x1b:
                case 0x2b:
                    driveInfo->dvdRPlus = true;
                    break;

                case 0x13:
                case 0x14:
                    driveInfo->dvdRWMinus = true;
                    break;

                case 0x1a:
                case 0x2a:
                    driveInfo->dvdRWPlus = true;
                    break;

                case 0x09:
                    driveInfo->cdR = true;
                    break;

                case 0x0a:
                    driveInfo->cdRW = true;
                    break;

                case 0x41:
                case 0x42:
                    driveInfo->bdR = true;
                    break;

                }

                pos++;
            }
        }
    }
    else if(currentOperation() == OPER_DISC_INFO)
    {
        QRegExp rx("Format status: ([^\n]*)\n");
        int pos = rx.indexIn(str) + rx.matchedLength();
        if(pos > 0)
        {
            QString formatStr = rx.cap(1);
            if((pos = formatStr.indexOf("unformatted,") == 0))
            {
                mDiscFormat = BURN_FORMAT_IS_UNFORMATTED;
                int pos1 = formatStr.indexOf(" ", pos + 18);
                mDiscFormatSize = formatStr.mid(pos + 18, pos1 - pos - 18).toFloat() * 1024 * 1024;
            }
            else if(formatStr.indexOf("formatted,") == 0)
            {
                mDiscFormat = BURN_FORMAT_IS_FORMATTED;
                int pos1 = formatStr.indexOf(" ", pos + 15);
                mDiscFormatSize = formatStr.mid(pos + 15, pos1 - pos - 15).toFloat() * 1024 * 1024;
            }
            else if(formatStr.indexOf("written,") == 0)
            {
                mDiscFormat = BURN_FORMAT_IS_FORMATTED;
                int pos1 = formatStr.indexOf(" ", pos + 13);
                mDiscFormatSize = formatStr.mid(pos + 13, pos1 - pos - 13).toFloat() * 1024 * 1024;
            }
        }

        pos = 0;
        while((pos = str.indexOf("Format idx ", pos)) >= 0)
        {
            mDiscFormatNum++;
            pos++;
        }
    }
}

void DiscRec::clearDrives()
{
    for (QList<struct burn_drive*>::iterator it = mDrivesGrabbedList.begin(); it != mDrivesGrabbedList.end(); it++)
        releaseDrive(*it);
    mDrivesGrabbedList.clear();

    for(int i = 0; i < mDrivesCount; i++)
    {
        struct burn_drive_info* driveInfo = mDrives[i];
        struct burn_drive *drive = driveInfo->drive;

        delete drive;
        delete driveInfo;
    }

    mDrivesCount = 0;
    mDrivesNamesMap.clear();
    mDrives.clear();
}

bool DiscRec::initialize()
{
    setCurrentOperation(OPER_INITIALIZING);

    emit finished(OPER_INITIALIZING, true); // завершение операции
    setCurrentOperation(OPER_NO);
    return true;
}

bool DiscRec::shutdown()
{
    bool result = false;
    switch (currentOperation())
    {
    case OPER_SCANNING_RECORDERS:
        emit message(MSG_CRITICAL, TR("Завершение работы невозможно, так как выполняется поиск устройств"));
        goto quit;
    case OPER_CREATION_ISO:

    case OPER_RECORDING_DISC:
    case OPER_ERASING_DISC:
    case OPER_FORMATTING_DISC:
        emit message(MSG_CRITICAL, TR("Завершение работы невозможно, так как выполняется создание образа или запись/стирание/форматирование диска"));
        goto quit;
    case OPER_CANCELLING:
        emit message(MSG_CRITICAL, TR("Завершение работы невозможно, так как выполняется отмена создания образа или записи/стирания/форматирования диска"));
        goto quit;
    case OPER_INITIALIZING:
        emit message(MSG_CRITICAL, TR("Завершение работы невозможно, так как выполняется инициализация библиотек"));
        goto quit;
        break;
    case OPER_DRIVE_INFO: // можно попытаться завершить работу, но возможны ошибки из-за захваченного устройства
    case OPER_DISC_INFO:
        break;
    case OPER_SHUTDOWNING: // второй раз не надо вызывать - вернем ошибку
        emit message(MSG_CRITICAL, TR("Завершение работы невозможно, так как выполняется инициализация библиотек"));
        goto quit;
    case OPER_NO: // все ок, завершаем работу
        break;
    }
    setCurrentOperation(OPER_SHUTDOWNING);

    clearDrives();

    result = true;

quit:
    setCurrentOperation(OPER_NO);
    emit finished(OPER_SHUTDOWNING, result); // завершение операции
    return result;
}

void DiscRec::cancel()
{
    OperationType currOper = currentOperation();
    if (currOper != OPER_NO && currOper != OPER_CANCELLING)
    {
        emit message(MSG_CURRENT_OPERATION, TR("Прерывание операции %1").arg(
                         (currOper == OPER_SCANNING_RECORDERS) ? TR("поиска устройств") :
                         (currOper == OPER_CREATION_ISO) ? TR("создания образа") :
                         (currOper == OPER_RECORDING_DISC) ? TR("записи диска") :
                         (currOper == OPER_ERASING_DISC) ? TR("стирания диска") :
                         (currOper == OPER_FORMATTING_DISC) ? TR("форматирования диска") :
                         (currOper == OPER_INITIALIZING) ? TR("инициализации библиотек") :
                         (currOper == OPER_SHUTDOWNING) ? TR("завершения работы с библиотеками") :
                         (currOper == OPER_DRIVE_INFO) ? TR("получения информации о приводе") :
                         (currOper == OPER_DISC_INFO) ? TR("получения информации о диске") : ""));
        setCurrentOperation(OPER_CANCELLING);
    }
}

void DiscRec::cancelAndWait()
{
    cancel();
    while (currentOperation() != OPER_NO) ;
}

OperationType DiscRec::currentOperation()
{
    QReadLocker locker(&mCurrOperLock);
    return mCurrOper;
}

void DiscRec::setCurrentOperation(OperationType currOper)
{
    QWriteLocker locker(&mCurrOperLock);
    mCurrOper = currOper;
}

bool DiscRec::makeISO(QString discTitle, QString dirPath, QString isoPath, QString excludeFilePath, bool isFirstWrite)
{
    setCurrentOperation(OPER_CREATION_ISO);

//    qDebug() << "MAKE ISO" << discTitle << dirPath << excludeFilePath;


    /*! Необходимые объекты и некоторые конфиги */
    int retValue;
    QStringList args;

    // Результат выполнения операции
    int result = 1;

    // Каталог с файлами на запись
    QDir filesDir(dirPath);

    // Файл с образом на запись
    QFile isoFile(isoPath);

    if (!filesDir.exists())
    {
        result = -1;
        emit message(MSG_CRITICAL, TR("Каталог с файлами на запись не найден"));
        goto quit;
    }

/*    if( !untar(Const::loaderArc, dirPath) )
    {
        result = -1;
        emit message(MSG_CRITICAL, TR("Невозможно распаковать файлы для создания загрузочного диска"));
        goto quit;
    }*/

    /////////////////////// Заглушка ///////////////////////

    emit message(MSG_CURRENT_OPERATION, TR("Создание образа на запись"));
    emit message(MSG_EXECUTION_PERCENT, "0");

    //! Скопируем загрузочный каталог в каталог с файлами на запись
/*    if (!copyDir(Const::loaderPath, dirPath))
    {
        result = -1;
        emit message(MSG_CRITICAL, TR("Ошибка копирования загрузочного каталога в каталог с файлами на запись"));
        goto delete_loader_from_dest;
    }*/
    //emit message(MSG_EXECUTION_PERCENT, "25");

    //! Если поступила команда прерывания процесса, выходим
    if (currentOperation() == OPER_CANCELLING)
    {
        result = -1;
        goto quit;
    }

    //! Если раньше был сформирован образ, удалим его
    if (isoFile.exists()) isoFile.remove();

    args.clear();
    //args << "-no-pad" << "-b" << "isolinux/isolinux.bin" << "-c" << "isolinux/boot.cat" << "-no-emul-boot" << "-boot-load-size" << "4" << "-boot-info-table" << "-r" << "-V" << discTitle << "-o" << isoPath << dirPath;
    args << "-as" << "mkisofs" << "-no-pad";
    if(isFirstWrite)
    {
        args << "-b" << "isolinux/isolinux.bin" << "-c" << "isolinux/boot.cat" << "-no-emul-boot" << "-boot-load-size" << "4" << "-boot-info-table";
    }
    args << "-r" << "-V" << discTitle << "-exclude-list" << excludeFilePath << "-o" << isoPath << dirPath;

    mPercent = 0;
    if(runXorriso(args))
    {
        while(mXorrisoWorking)
        {
            mXorrisoProcess->waitForFinished(10);

            if (currentOperation() == OPER_CANCELLING)
            {
                result = -1;
                break;
            }

            emit message(MSG_EXECUTION_PERCENT, QString::number(mPercent));
        }
    }    

    if(result == -1)
    {
        stopXorriso();
        goto quit;
    }

    if(mXorrisoRetCode)
    {
        result = -1;
        emit message(MSG_CRITICAL, TR("Ошибка создания образа на запись (возвращено ненулевое значение)"));
    }

quit:

    retValue = (result >= 0 && QFile::exists(Const::isoPath));
    emit message(MSG_EXECUTION_PERCENT, "100");
    emit finished(OPER_CREATION_ISO, retValue);
    setCurrentOperation(OPER_NO);
    return retValue;
}

bool DiscRec::recordISO(QString driveName, const char *isoPath)
{
    setCurrentOperation(OPER_RECORDING_DISC);

    QStringList args;
    int result = -1;
    struct burn_drive *drive = NULL;

    //! Получаем указатель на структуру привода
    drive = getDrive(driveName);
    if (!drive)
    {
        result = -1;
        goto quit;
    }

    //! Захват устройства
    result = grabDrive(drive);
    if (!result)
    {
        if (currentOperation() != OPER_CANCELLING)
            emit message(MSG_CRITICAL, TR("Ошибка захвата устройства привода"));
        goto quit;
    }

    emit message(MSG_CURRENT_OPERATION, TR("Запись диска"));
    emit message(MSG_EXECUTION_PERCENT, "0");

    args.clear();
    args << "-as" << "cdrecord" << "-v" << ("dev=" + drive->driveDevName) << "-dao" << isoPath;

    mPercent = 0;
    if(runXorriso(args))
    {
        while(mXorrisoWorking)
        {
            mXorrisoProcess->waitForFinished(10);

            if (currentOperation() == OPER_CANCELLING)
            {
                result = -1;
                break;
            }

            if(mPercent == 100)mPercent = 99;
            emit message(MSG_EXECUTION_PERCENT, QString::number(mPercent));
        }
    }

    if(result == -1)
    {
        stopXorriso();
        goto quit;
    }

    if(mXorrisoRetCode)
    {
        result = -1;
        emit message(MSG_CRITICAL, TR("Ошибка записи диска (возвращено ненулевое значение)"));
    }
    else
        emit message(MSG_EXECUTION_PERCENT, QString::number(100));

quit:
    //! Освобождаем привод
    releaseDrive(drive);

    setCurrentOperation(OPER_NO);
    //emit message(CurrentOperation, QString::null); // завершение операции
    emit finished(OPER_RECORDING_DISC, result > 0); // завершение операции
    return (result > 0);
}

bool DiscRec::eraseDisc(QString driveName, bool isFastBlank)
{
    setCurrentOperation(OPER_ERASING_DISC);

    QStringList args;
    int result = -1;
    struct burn_drive *drive = NULL;

    //! Получаем указатель на структуру привода
    drive = getDrive(driveName);
    if (!drive)
    {
        result = -1;
        goto quit;
    }

    //! Захват устройства
    result = grabDrive(drive);
    if (!result)
    {
        if (currentOperation() != OPER_CANCELLING)
            emit message(MSG_CRITICAL, TR("Ошибка захвата устройства привода"));
        goto quit;
    }

    emit message(MSG_CURRENT_OPERATION, TR("Стирание диска"));
    emit message(MSG_EXECUTION_PERCENT, "0");

    args.clear();
    args << "-as" << "cdrecord" << "-v" << ("dev=" + drive->driveDevName) << ("blank=" + QString(isFastBlank ? "fast" : "all"));

    mPercent = 0;
    if(runXorriso(args))
    {
        while(mXorrisoWorking)
        {
            mXorrisoProcess->waitForFinished(10);

            if (currentOperation() == OPER_CANCELLING)
            {
                result = -1;
                break;
            }

            emit message(MSG_EXECUTION_PERCENT, QString::number(mPercent));
        }
    }

    if(result == -1)
    {
        stopXorriso();
        goto quit;
    }

    if(mXorrisoRetCode)
    {
        result = -1;
        emit message(MSG_CRITICAL, TR("Ошибка стирания диска (возвращено ненулевое значение)"));
    }
    else
        emit message(MSG_EXECUTION_PERCENT, QString::number(100));

quit:
    //! Освобождаем привод
    releaseDrive(drive);

    setCurrentOperation(OPER_NO);
    //emit message(CurrentOperation, QString::null); // завершение операции
    emit finished(OPER_ERASING_DISC, result > 0); // завершение операции
    return (result > 0);
}

bool DiscRec::formatDisc(QString driveName)
{
    setCurrentOperation(OPER_FORMATTING_DISC);

    QStringList args;
    int result = -1;
    struct burn_drive *drive = NULL;

    //! Получаем указатель на структуру привода
    drive = getDrive(driveName);
    if (!drive)
    {
        result = -1;
        goto quit;
    }

    //! Захват устройства
    result = grabDrive(drive);
    if (!result)
    {
        if (currentOperation() != OPER_CANCELLING)
            emit message(MSG_CRITICAL, TR("Ошибка захвата устройства привода"));
        goto quit;
    }

    emit message(MSG_CURRENT_OPERATION, TR("Форматирование диска"));
    emit message(MSG_EXECUTION_PERCENT, "0");

    args.clear();
    args << "-outdev" << drive->driveDevName << "format";

    mPercent = 0;
    if(runXorriso(args))
    {
        while(mXorrisoWorking)
        {
            mXorrisoProcess->waitForFinished(10);

            if (currentOperation() == OPER_CANCELLING)
            {
                result = -1;
                break;
            }

            emit message(MSG_EXECUTION_PERCENT, QString::number(mPercent));
        }
    }

    if(result == -1)
    {
        stopXorriso();
        goto quit;
    }

    if(mXorrisoRetCode)
    {
        result = -1;
        emit message(MSG_CRITICAL, TR("Ошибка форматирования диска (возвращено ненулевое значение)"));
    }
    else
        emit message(MSG_EXECUTION_PERCENT, QString::number(100));

quit:
    //! Освобождаем привод
    releaseDrive(drive);

    setCurrentOperation(OPER_NO);
    //emit message(CurrentOperation, QString::null); // завершение операции
    emit finished(OPER_FORMATTING_DISC, result > 0); // завершение операции
    return (result > 0);
}

bool DiscRec::scanDrives()
{    
    setCurrentOperation(OPER_SCANNING_RECORDERS);

    QStringList args;
    int result = -1;

    emit message(MSG_CURRENT_OPERATION, TR("Поиск устройств"));
    mDrivesNamesMap.clear();
    mDrivesCount = 0;
    mDrives.clear();

    mScanDrives = true;
    args << "-devices";
    if(runXorriso(args))
    {
        result = 1;
        while(mXorrisoWorking)
        {
            mXorrisoProcess->waitForFinished(10);

            if (currentOperation() == OPER_CANCELLING)
            {
                result = -1;
                break;
            }
        }
        if(mXorrisoRetCode)
            result = -1;
    }

    if(result == 1)
    {
        int executionPercent = 0;
        int percentAppend = 100 / mDrivesCount;

        for(int i = 0; i < mDrivesCount; i++)
        {
            struct burn_drive_info* driveInfo = mDrives[i];
            struct burn_drive *drive = driveInfo->drive;

            driveInfo->dvdRAM = false;
            driveInfo->dvdRMinus = false;
            driveInfo->dvdRPlus = false;
            driveInfo->dvdRWMinus = false;
            driveInfo->dvdRWPlus = false;
            driveInfo->cdR = false;
            driveInfo->cdRW = false;
            driveInfo->bdR = false;
            driveInfo->dvdRAM = false;

            mScanDrives = false;
            mCurDrive = i;
            args.clear();
            args << "-outdev" << drive->driveDevName << "-list_profiles" << "out";
            if(runXorriso(args))
            {
                while(mXorrisoWorking)
                {
                    mXorrisoProcess->waitForFinished(10);

                    if (currentOperation() == OPER_CANCELLING)
                    {
                        result = -1;
                        break;
                    }
                }
                if(result == -1)
                    break;
            }
            else
            {
                result = -1;
                break;
            }

            if(mXorrisoRetCode)
            {
                result = -1;
                break;
            }

            executionPercent += percentAppend;
            if(executionPercent > 100) executionPercent = 100;
            emit message(MSG_EXECUTION_PERCENT, QString::number(executionPercent));
        }
    }

    if(result == -1)
    {
        stopXorriso();
        clearDrives();
    }

    setCurrentOperation(OPER_NO);
    //emit message(CurrentOperation, QString::null); // завершение операции
    emit finished(OPER_SCANNING_RECORDERS, result > 0); // завершение операции
    return (result > 0);
}

QList<QString> DiscRec::drivesList() const
{
    return mDrivesNamesMap.values();
}

DiscInfo DiscRec::discInfo(QString driveName)
{
    emit message(MSG_EXECUTION_PERCENT, "0");
    emit message(MSG_CURRENT_OPERATION, TR("Получение информации о диске"));
    DiscInfo discInfo = DiscInfo(driveName);
    emit message(MSG_EXECUTION_PERCENT, "100");
    emit finished(OPER_DISC_INFO, discInfo.isValid()); // завершение операции
    return discInfo;
}

DriveInfoConst DiscRec::driveInfoConst(QString driveName)
{
    emit message(MSG_CURRENT_OPERATION, TR("Получение информации о приводе"));
    DriveInfoConst driveInfo = DriveInfoConst(driveName);
    emit finished(OPER_DRIVE_INFO,
                  driveInfo.driveName().size() && driveInfo.driveVendor().size() &&
                  driveInfo.driveProduct().size() && driveInfo.driveRevision().size() &&
                  driveInfo.driveLocation().size()); // завершение операции
    return driveInfo;
}

int DiscRec::getDriveNumber(QString driveName)
{
    // Номер привода в карте
    const int driveNumber = mDrivesNamesMap.key(driveName, -1);

    if (driveNumber == -1)
        emit message(MSG_CRITICAL, TR("Выбранный привод не найден. Повторите процедуру обнаружения приводов"));

    return driveNumber;
}

burn_drive_info* DiscRec::getDriveInfo(QString driveName)
{
    // Структура с информацией о приводе
    struct burn_drive_info* drvInfo = 0;

    // Номер привода в карте
    const int driveNumber = getDriveNumber(driveName);

    if (driveNumber != -1)
    {
        drvInfo = mDrives[driveNumber];
        if (!drvInfo)
            emit message(MSG_CRITICAL, TR("Информация о выбранном приводе не найдена. Повторите процедуру обнаружения приводов"));
    }

    return drvInfo;
}

burn_drive* DiscRec::getDrive(QString driveName)
{
    // Структура привода
    struct burn_drive* drv = 0;

    // Структура с информацией о приводе
    struct burn_drive_info* drvInfo = 0;

    drvInfo = getDriveInfo(driveName);

    if (drvInfo)
    {
        drv = drvInfo->drive;
        if (!drv)
            emit message(MSG_CRITICAL, TR("Структура выбранного привода не найдена. Повторите процедуру обнаружения приводов"));
    }

    return drv;
}

bool DiscRec::grabDrive(burn_drive* drive)
{
    if (!drive)
    {
        emit message(MSG_CRITICAL, TR("Захват устройства привода не выполнен, т.к. он не задан"));
        return false;
    }

    mDrivesGrabbedListLock.lockForWrite(); // если кто-то из другого потока хочет прочитать статус захвата пусть обломится и подождет
    if (mDrivesGrabbedList.contains(drive))
    {
        //emit message(Critical, TR("Ошибка захвата устройства привода, т.к. оно уже было захвачено"));
        mDrivesGrabbedListLock.unlock();
        return true;
    }

    mDrivesGrabbedList.append(drive);
    mDrivesGrabbedListLock.unlock();

    return true;
}

bool DiscRec::grabDrive(QString driveName)
{
    burn_drive* drive = getDrive(driveName);

    if (drive)
        return grabDrive(drive);

    return false;
}

bool DiscRec::releaseDrive(burn_drive* drive)
{

    if (!drive)
    {
        emit message(MSG_CRITICAL, TR("Особождение устройства привода не выполнено, т.к. оно не задано"));
        return false;
    }
    mDrivesGrabbedListLock.lockForWrite(); // если кто-то из другого потока хочет прочитать статус захвата пусть обломится и подождет
    int index = mDrivesGrabbedList.indexOf(drive);
    //! Освобождаем привод
    if (index != -1)
    {
        mDrivesGrabbedList.removeAt(index);
    }
    mDrivesGrabbedListLock.unlock();

    return true;
}

bool DiscRec::releaseDrive(QString driveName)
{
    burn_drive* drive = getDrive(driveName);

    if (drive)
        return releaseDrive(drive);

    return false;
}

bool DiscRec::isDriveGrabbed(burn_drive* drive)
{
    if (!drive)
    {
        emit message(MSG_CRITICAL, TR("Невозможно получить статус захвата привода, т.к. привод не задан"));
        return false;
    }
    QReadLocker locker(&mDrivesGrabbedListLock);
    return mDrivesGrabbedList.contains(drive);
}

bool DiscRec::isDriveGrabbed(QString driveName)
{
    burn_drive* drive = getDrive(driveName);

    if (drive)
        return isDriveGrabbed(drive);

    return false;
}

burn_disc_status DiscRec::getDiscStatus(burn_drive* drive)
{
    burn_disc_status discStatus = BURN_DISC_UNREADY;
    if (!drive)
    {
        emit message(MSG_CRITICAL, TR("Невозможно получить статус диска, т.к. привод не задан"));
        return discStatus;
    }

    return mDiscStatus;
}

burn_disc_status DiscRec::getDiscStatus(QString driveName)
{
    burn_drive* drive = getDrive(driveName);

    if (drive)
        return getDiscStatus(drive);

    return BURN_DISC_UNREADY;
}

bool DiscRec::getDiscFormatType(burn_drive* drive, int& formatStatus, int& numFormats, off_t& formatSize)
{
    if (!drive)
    {
        emit message(MSG_CRITICAL, TR("Невозможно получить статус форматирования диска, т.к. привод не задан"));
        return false;
    }

    mDiscFormat = BURN_FORMAT_IS_UNKNOWN;
    mDiscFormatSize = 0;
    mDiscFormatNum = 0;

    int result = 0;

    QStringList args;
    args.clear();
    args << "-outdev" << drive->driveDevName << "-list_formats";
    if(runXorriso(args))
    {
        while(mXorrisoWorking)
        {
            mXorrisoProcess->waitForFinished(10);

            if (currentOperation() == OPER_CANCELLING)
            {
                result = 1;
                stopXorriso();
                break;
            }
        }
    }

    if(mXorrisoRetCode)
        result = 1;

    if(result)
        return false;

    formatStatus = mDiscFormat;
    formatSize = mDiscFormatSize;
    numFormats = mDiscFormatNum;

    return true;
}

bool DiscRec::getDiscFormatType(QString driveName, int& formatStatus, int& numFormats, off_t& formatSize)
{
    burn_drive* drive = getDrive(driveName);

    if (drive)
        return getDiscFormatType(drive, formatStatus, numFormats, formatSize);

    return false;
}

//burn_drive_status DiscRec::getDriveStatus(burn_drive* drive)
//{
//    if (!drive)
//    {
//        emit message(MSG_CRITICAL, TR("Невозможно получить статус привода, т.к. привод не задан"));
//        return BURN_DRIVE_IDLE;
//    }
//    return BURN_DRIVE_IDLE;
//    //return burn_drive_get_status(drive, progress);
//}

//burn_drive_status DiscRec::getDriveStatus(QString driveName)
//{
//    burn_drive* drive = getDrive(driveName);

//    if (drive)
//        return getDriveStatus(drive);

//    return BURN_DRIVE_IDLE;
//}

bool DiscRec::getDiscType(burn_drive* drive, int& discType, QString& discTypeName)
{
    if (!drive)
    {
        emit message(MSG_CRITICAL, TR("Невозможно получить тип диска, т.к. привод не задан"));
        return false;
    }

    mDiscProfileStr = "";
    mDiscStatus = BURN_DISC_UNREADY;

    QStringList args;
    args.clear();
    args << "-outdev" << drive->driveDevName << "-list_profiles" << "out";
    if(runXorriso(args))
    {
        while(mXorrisoWorking)
        {
            mXorrisoProcess->waitForFinished(10);

            if (currentOperation() == OPER_CANCELLING)
            {
                stopXorriso();
                break;
            }
        }
    }

    if(mXorrisoRetCode || mDiscProfileStr.isEmpty() || (discType = mProfilesMap.key(mDiscProfileStr, -1)) == -1)
        return false;

    discTypeName = mDiscProfileStr;

    return true;
}

bool DiscRec::getDiscType(const QString driveName, int& discType, QString& discTypeName)
{
    burn_drive* drive = getDrive(driveName);

    if (drive)
        return getDiscType(drive, discType, discTypeName);

    return false;
}

bool DiscRec::isDiscErasable(burn_drive* drive)
{
    if (!drive)
    {
        emit message(MSG_CRITICAL, TR("Невозможно получить информацию о том, перезаписываемый ли диск, т.к. привод не задан"));
        return false;
    }

    int discType = mProfilesMap.key(mDiscProfileStr, -1);
    if(discType == 0x0a || discType == 0x13 || discType == 0x14 || discType == 0x1a || discType == 0x2a)
        return true;

    return false;
}

bool DiscRec::isDiscErasable(const QString driveName)
{
    burn_drive* drive = getDrive(driveName);

    if (drive)
        return isDiscErasable(drive);

    return false;
}

//int DiscRec::getMaxWriteSpeed(burn_drive* drive)
//{
//    if (!drive)
//    {
//        emit message(MSG_CRITICAL, TR("Невозможно получить информацию о максимальной скорости записи на диск, т.к. привод не задан"));
//        return 0;
//    }

//    return 0;//(burn_drive_get_write_speed(drive));
//}

//int DiscRec::getMaxWriteSpeed(const QString driveName)
//{
//    burn_drive* drive = getDrive(driveName);

//    if (drive)
//        return getMaxWriteSpeed(drive);

//    return 0;
//}

//int DiscRec::getMinWriteSpeed(burn_drive* drive)
//{
//    if (!drive)
//    {
//        emit message(MSG_CRITICAL, TR("Невозможно получить информацию о минимальной скорости записи на диск, т.к. привод не задан"));
//        return 0;
//    }

//    return 0;//(burn_drive_get_min_write_speed(drive));
//}

//int DiscRec::getMinWriteSpeed(const QString driveName)
//{
//    burn_drive* drive = getDrive(driveName);

//    if (drive)
//        return getMinWriteSpeed(drive);

//    return 0;
//}

//int DiscRec::getMaxReadSpeed(burn_drive* drive)
//{
//    if (!drive)
//    {
//        emit message(MSG_CRITICAL, TR("Невозможно получить информацию о максимальной скорости чтения привода, т.к. привод не задан"));
//        return 0;
//    }

//    return 0;//(burn_drive_get_read_speed(drive));
//}

//int DiscRec::getMaxReadSpeed(const QString driveName)
//{
//    burn_drive* drive = getDrive(driveName);

//    if (drive)
//        return getMaxReadSpeed(drive);

//    return 0;
//}

off_t DiscRec::getAvailableDiscSpace(burn_drive* drive)
{
    if (!drive)
    {
        emit message(MSG_CRITICAL, TR("Невозможно получить информацию о доступной свободной емкости диска, т.к. привод не задан"));
        return 0;
    }

    return mDiskFreeSize;
}

off_t DiscRec::getAvailableDiscSpace(const QString driveName)
{
    burn_drive* drive = getDrive(driveName);

    if (drive)
        return getAvailableDiscSpace(drive);

    return 0;
}

void DiscRec::sendCurrentDriveStatus(const burn_drive_status& driveStatus)
{
    QString drvStatusStr = (driveStatus == BURN_DRIVE_IDLE ) ? TR("Нет операций на выполнение") :
                           (driveStatus == BURN_DRIVE_SPAWNING) ? TR("Подготовка библиотеки к записи") :
                           (driveStatus == BURN_DRIVE_READING) ? TR("Чтение данных с диска") :
                           (driveStatus == BURN_DRIVE_WRITING) ? TR("Запись данных на диск") :
                           (driveStatus == BURN_DRIVE_WRITING_LEADIN) ? TR("Запись Lead-In") :
                           (driveStatus == BURN_DRIVE_WRITING_LEADOUT) ? TR("Запись Lead-Out") :
                           (driveStatus == BURN_DRIVE_ERASING) ? TR("Стирание диска") :
                           (driveStatus == BURN_DRIVE_GRABBING) ? TR("Захват устройства") :
                           (driveStatus == BURN_DRIVE_WRITING_PREGAP) ? TR("Подготовка к записи трека") :
                           (driveStatus == BURN_DRIVE_CLOSING_TRACK) ? TR("Закрытие трека") :
                           (driveStatus == BURN_DRIVE_CLOSING_SESSION) ? TR("Закрытие сессии") :
                           (driveStatus == BURN_DRIVE_CLOSING_SESSION) ? TR("Форматирование диска") :
                           (driveStatus == BURN_DRIVE_READING_SYNC) ? TR("Синхронное чтение") :
                           (driveStatus == BURN_DRIVE_WRITING_SYNC) ? TR("Синхронная запись") : "";
    if (drvStatusStr.size())
        emit message(MSG_CURRENT_OPERATION, drvStatusStr);
}

//bool DiscRec::copyDir(QString srcDirPath, QString dstDirPath)
//{
//    QDir dir(srcDirPath);
//    if (!dir.exists())
//        return false;

//    // Проход по каталогам и рекурсивный вызов самой себя для каталога
//    foreach (QString d, dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
//    {
//        QString dstPath = dstDirPath + QDir::separator() + d;
//        if (!dir.mkpath(dstPath))
//            return false;
//        if (!copyDir(srcDirPath+ QDir::separator() + d, dstPath))
//            return false;
//    }

//    // Проход по файлам каталога
//    foreach (QString f, dir.entryList(QDir::Files))
//    {
//        // Удалим из места назначения
//        QFile::remove(dstDirPath + QDir::separator() + f);

//        if (!QFile::copy(srcDirPath + QDir::separator() + f, dstDirPath + QDir::separator() + f))
//            return false;
//    }
//    return true;
//}

//bool DiscRec::removeDir(const QString &dirPath)
//{
//    QDir dir(dirPath);
//    if (!dir.exists())
//        return false;

//    // Находим и удаляем файлы и папки
//    foreach(const QFileInfo &info, dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot))
//    {
//        if (info.isDir()) // если каталог, то рекурсивный вызов самой себя для этого каталога
//        {
//            if (!removeDir(info.filePath()))
//                return false;
//        }
//        else // в противном случае удаление файла
//        {
//            if (!dir.remove(info.fileName()))
//                return false;
//        }
//    }

//    // Удаляем каталог dirPath
//    QDir parentDir(QFileInfo(dirPath).path());
//    return parentDir.rmdir(QFileInfo(dirPath).fileName());
//}

////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////// DiscInfo /////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

DiscInfo::DiscInfo()
{
}

DiscInfo::DiscInfo(QString driveName)
    :mDriveName(driveName),
     mIsValid(false),
     mDiscType(-100),
     mDiscTypeName(""),
     mDiscStatus(BURN_DISC_UNREADY),
     mFormatStatus(-1),
     mNumFormats(-1),
     mFormatSize(-1),
     mDriveStatus(BURN_DRIVE_IDLE),
     mIsDiscErasable(false)
{
    update();
}

QString DiscInfo::driveName()
{
    return mDriveName;
}

bool DiscInfo::isValid() const
{
    return mIsValid;
}

void DiscInfo::invalid()
{
    mIsValid = false;
}

bool DiscInfo::update()
{
    DiscRec::instance().setCurrentOperation(OPER_DISC_INFO);

    mIsValid = false;
    mDiscType = -100;
    mDiscTypeName = "";
    mDiscStatus = BURN_DISC_UNREADY;    
    //mDiscFormatTypeResult = false;
    mFormatStatus = -1;
    mNumFormats = -1;
    mFormatSize = -1;
    mDriveStatus = BURN_DRIVE_IDLE;
    mIsDiscErasable = false;

    ////emit message(MSG_EXECUTION_PERCENT, QString::number(0));

    if (!DiscRec::instance().grabDrive(mDriveName))
    {
        mIsValid = false;
        goto quit;
    }
    if (DiscRec::instance().currentOperation() == OPER_CANCELLING)
    {
        mIsValid = false;
        goto quit;
    }

    //! Получим тип диска и описание типа
    if (!DiscRec::instance().getDiscType(mDriveName, mDiscType, mDiscTypeName))
    {
        mIsValid = false;
        goto quit;
    }
  //  emit message(MSG_EXECUTION_PERCENT, QString::number(25));
    if (DiscRec::instance().currentOperation() == OPER_CANCELLING)
    {
        mIsValid = false;
        goto quit;
    }

    //! Получим статус диска
    if ((mDiscStatus = DiscRec::instance().getDiscStatus(mDriveName)) == BURN_DISC_UNREADY)
    {
        mIsValid = false;
        goto quit;
    }
    if (DiscRec::instance().currentOperation() == OPER_CANCELLING)
    {
        mIsValid = false;
        goto quit;
    }

    //! Получим статус форматирования диска
    //if (!(mDiscFormatTypeResult = DiscRec::instance().getDiscFormatType(mFormatStatus, mNumFormats, mFormatSize)))
    if (!DiscRec::instance().getDiscFormatType(mDriveName, mFormatStatus, mNumFormats, mFormatSize))
    {
        mIsValid = false;
        goto quit;
    }
   //emit message(MSG_EXECUTION_PERCENT, QString::number(50));
    if (DiscRec::instance().currentOperation() == OPER_CANCELLING)
    {
        mIsValid = false;
        goto quit;
    }

    //! Получим статус привода
   // mDriveStatus = DiscRec::instance().getDriveStatus(mDriveName, &mBurnProgress);
//    if (DiscRec::instance().currentOperation() == OPER_CANCELLING)
//    {
//        mIsValid = false;
//        goto quit;
//    }

//    //! Узнаем стираемый ли диск
    //if (mDiscStatus == BURN_DISC_FULL)
        mIsDiscErasable = DiscRec::instance().isDiscErasable(mDriveName);
    if (DiscRec::instance().currentOperation() == OPER_CANCELLING)
    {
        mIsValid = false;
        goto quit;
    }

    //! Узнаем скорости чтения и записи
//    mMaxWriteSpeed = DiscRec::instance().getMaxWriteSpeed(mDriveName);
//    if (DiscRec::instance().currentOperation() == OPER_CANCELLING)
//    {
//        mIsValid = false;
//        goto quit;
//    }

//    mMinWriteSpeed = DiscRec::instance().getMinWriteSpeed(mDriveName);
//    if (DiscRec::instance().currentOperation() == OPER_CANCELLING)
//    {
//        mIsValid = false;
//        goto quit;
//    }

//    mMaxReadSpeed = DiscRec::instance().getMaxReadSpeed(mDriveName);
//    if (DiscRec::instance().currentOperation() == OPER_CANCELLING)
//    {
//        mIsValid = false;
//        goto quit;
//    }

    mAvailableDiscSpace = DiscRec::instance().getAvailableDiscSpace(mDriveName);
    if (DiscRec::instance().currentOperation() == OPER_CANCELLING)
    {
        mIsValid = false;
        goto quit;
    }

    mIsValid = true;

quit:
    //DiscRec::instance().releaseDrive(mDriveName);
    DiscRec::instance().setCurrentOperation(OPER_NO);
    return mIsValid;
}

bool DiscInfo::getDiscType(int& discType, QString& discTypeName) const
{
    if (!mDiscTypeName.size())
        return false;
    discType = mDiscType;
    discTypeName = mDiscTypeName;
    return true;
}

burn_disc_status DiscInfo::getDiscStatus() const
{
    return mDiscStatus;
}

bool DiscInfo::getDiscFormatType(int &formatStatus, int &numFormats, off_t &formatSize) const
{
    if (mFormatStatus == -1)
        return false;
    formatStatus = mFormatStatus;
    numFormats = mNumFormats;
    formatSize = mFormatSize;
    return true;
}

burn_drive_status DiscInfo::getDriveStatus() const
{
    return mDriveStatus;
}

bool DiscInfo::isDiscErasable() const
{
    return mIsDiscErasable;
}

int DiscInfo::getMaxWriteSpeed() const
{
    return mMaxWriteSpeed;
}

int DiscInfo::getMinWriteSpeed() const
{
    return mMinWriteSpeed;
}

int DiscInfo::getMaxReadSpeed() const
{
    return mMaxReadSpeed;
}

off_t DiscInfo::getAvailableDiscSpace() const
{
    return mAvailableDiscSpace;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////// DriveInfo ////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

DriveInfoConst::DriveInfoConst()
{
}

DriveInfoConst::DriveInfoConst(QString driveName)
    :mDriveName(driveName)
{
}

QString DriveInfoConst::driveName()
{
    return mDriveName;
}

QString DriveInfoConst::driveVendor()
{
    struct burn_drive_info* drvInfo = DiscRec::instance().getDriveInfo(mDriveName);
    if (drvInfo)
        return drvInfo->vendor;
    return "";
}

QString DriveInfoConst::driveProduct()
{
    struct burn_drive_info* drvInfo = DiscRec::instance().getDriveInfo(mDriveName);
    if (drvInfo)
        return drvInfo->product;
    return "";
}

QString DriveInfoConst::driveRevision()
{
    struct burn_drive_info* drvInfo = DiscRec::instance().getDriveInfo(mDriveName);
    if (drvInfo)
        return drvInfo->revision;
    return "";
}

QString DriveInfoConst::driveLocation()
{
    struct burn_drive_info* drvInfo = DiscRec::instance().getDriveInfo(mDriveName);
    if (drvInfo)
        return drvInfo->location;
    return "";
}

bool DriveInfoConst::driveIsDvdRam()
{
    struct burn_drive_info* drvInfo = DiscRec::instance().getDriveInfo(mDriveName);
    if (drvInfo)
        return drvInfo->dvdRAM;
    return false;
}

bool DriveInfoConst::driveIsDvdRPlus()
{
    struct burn_drive_info* drvInfo = DiscRec::instance().getDriveInfo(mDriveName);
    if (drvInfo)
        return drvInfo->dvdRPlus;
    return false;
}

bool DriveInfoConst::driveIsDvdRMinus()
{
    struct burn_drive_info* drvInfo = DiscRec::instance().getDriveInfo(mDriveName);
    if (drvInfo)
        return drvInfo->dvdRMinus;
    return false;
}

bool DriveInfoConst::driveIsDvdRWMinus()
{
    struct burn_drive_info* drvInfo = DiscRec::instance().getDriveInfo(mDriveName);
    if (drvInfo)
        return drvInfo->dvdRWMinus;
    return false;
}

bool DriveInfoConst::driveIsDvdRWPlus()
{
    struct burn_drive_info* drvInfo = DiscRec::instance().getDriveInfo(mDriveName);
    if (drvInfo)
        return drvInfo->dvdRWPlus;
    return false;
}

bool DriveInfoConst::driveIsCdR()
{
    struct burn_drive_info* drvInfo = DiscRec::instance().getDriveInfo(mDriveName);
    if (drvInfo)
        return drvInfo->cdR;
    return false;
}

bool DriveInfoConst::driveIsCdRW()
{
    struct burn_drive_info* drvInfo = DiscRec::instance().getDriveInfo(mDriveName);
    if (drvInfo)
        return drvInfo->cdRW;
    return false;
}

bool DriveInfoConst::driveIsBdR()
{
    struct burn_drive_info* drvInfo = DiscRec::instance().getDriveInfo(mDriveName);
    if (drvInfo)
        return drvInfo->bdR;
    return false;
}

