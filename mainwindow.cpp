#include <linux/cdrom.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <iostream>
#include <fstream>

#include <QTimer>
#include <QDir>
#include <QDebug>
#include <QProgressBar>
#include <QMessageBox>
#include <QCloseEvent>
#include <QMutexLocker>
#include <QApplication>

#include <zguilib/global_gui.h>
#include <zcommonlib/bytebuffer.h>
#include <zcommonlib/commonutil.h>
#include <json/json.h>

#include "mainwindow.h"
#include "ui_mainwindow.h"

using namespace commonlib;

MainWindow::MainWindow(QWidget* parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    mProgBar(0),
    mDiscRecWorkingThread(0),
    mLastSuccessOperation(OPER_NO),
    mTimerUpdateDiscInfo(0),
    mIsDevFileSuccessOpened(false)
{
    ui->setupUi(this);

    // Спрячем ненужные элементы окна
    ui->leFilesPath->hide();
    ui->label->hide();
    ui->btnSelectPath->hide();
    //#
    //ui->statusBar->hide();

    //! Регистрация мета-типов для работы сигналов и слотов
    qRegisterMetaType<MessageType>("MessageType");
    qRegisterMetaType<OperationType>("OperationType");
    qRegisterMetaType<DriveInfoConst>("DriveInfoConst");
    qRegisterMetaType<DiscInfo>("DiscInfo");

    //setFixedHeight(height());
    ui->menuBar->setVisible(false);
    ui->mainToolBar->setVisible(false);

    //! Настройка прогресс бара и статус бара
    mProgBar = new QProgressBar(this);
    mProgBar->setFormat("Текущая операция %p%");
    mProgBar->setMinimum(0);
    mProgBar->setMaximum(100);
    mProgBar->setValue(0);
    ui->statusBar->addPermanentWidget(mProgBar, 0);
    mProgBar->setVisible(false);

    mDiscIsChanged = true;

    //! Установка дефолтного каталога на запись
    QString mBaseDirName = QApplication::arguments()[1];
//    mBaseDirName = "/mnt/raid/disc";
    QDir dir(mBaseDirName);
    if(!dir.exists()) QApplication::exit(1);
    ui->leFilesPath->setText(mBaseDirName);

    mDiscInfoTryCounts = 0;

    mSplitProcess = NULL;
    deleteSplitFiles();
    if(!updateMetaFile())
    {
        ::exit(EXIT_ERROR);
    }

    QList<QString> temp;
    mTotalFilesSize = getFilesSizeInDir(mBaseDirName, temp);
    mMainFilesSize = getFilesSizeInDir(mBaseDirName + Const::backupDir, temp);
    mWrittenSize = 0;
    mSizeForWrite = 0;
    mDiscNumber = 1;
    mFirstWrite = true;
    showRecordedInfo();

    //! Создание и запуск рабочего потока
    mDiscRecWorkingThread = new DiscRecWorkingThread(this);
    mDiscRecWorkingThread->start();

    //! Инициализация библиотек
    ui->centralWidget->setEnabled(true);
    mDiscRecWorkingThread->initialize();

    //! Получение сообщений
    connect(&DiscRec::instance(),
            SIGNAL(message(MessageType, QString)),
            SLOT(slotReceiveMessage(MessageType, QString)));

    //! Получение сигнала о завершении операции
    connect(&DiscRec::instance(),
            SIGNAL(finished(OperationType, bool)),
            SLOT(slotOperationFinished(OperationType, bool)));

    //! Получение информации о приводе и диске от библиотеки
    connect(mDiscRecWorkingThread, SIGNAL(sigReceivedDriveInfo(DriveInfoConst)), this, SLOT(slotReceiveDriveInfo(DriveInfoConst)));
    connect(mDiscRecWorkingThread, SIGNAL(sigReceivedDiscInfo(DiscInfo)), this, SLOT(slotReceiveDiscInfo(DiscInfo)));

    //! Смена привода
    connect(ui->coBoxRecorder,
            SIGNAL(currentIndexChanged(int)),
            SLOT(slotRecorderChanged(int)));

    //! Выбор пути к записываемым файлам при нажатии на кнопку
    connect(ui->btnSelectPath,
            SIGNAL(clicked()),
            SLOT(slotSelectPath()));

    //! Изменение пути к записываемым файлам в поле ввода
    connect(ui->leFilesPath,
            SIGNAL(textChanged(const QString&)),
            SLOT(slotFilesPathTextChanged(const QString&)));

    //! Запись
    connect(ui->btnRecord,
            SIGNAL(clicked()),
            SLOT(slotRecord()));

    //! Отмена
    connect(ui->btnCancel,
            SIGNAL(clicked()),
            SLOT(slotCancel()));

    //! Нажатие на кнопку поиска приводов
    connect(ui->btnScanRecorders,
            SIGNAL(clicked()),
            SLOT(slotScanRecorders()));

    //! Обнаружение и добавление приводов
    slotScanRecorders();

    //! Таймер на обновление инфомрации о диске и приводе
    mTimerUpdateDiscInfo = new QTimer(this);
    connect(mTimerUpdateDiscInfo, SIGNAL(timeout()), this, SLOT(updateDiscDriveInfoFileCheck()));
    mTimerUpdateDiscInfo->start(3000);

    setWindowFlags( Qt::WindowTitleHint | Qt::WindowMinimizeButtonHint );
}

void MainWindow::showRecordedInfo()
{
    setWindowTitle(QString(TR("Запись диска (записано %1 из %2 байт)")).arg(mWrittenSize).arg(mTotalFilesSize));
}

bool MainWindow::runSplit(QStringList args)
{
    if(mSplitProcess)
    {
        mSplitProcess->close();
    }

    mSplitRetCode = 0;
    mSplitProcess = new QProcess();
    mSplitProcess->setWorkingDirectory(mBaseDirName + Const::backupDir);

    connect(mSplitProcess, SIGNAL(started()), this, SLOT(slotSplitStarted()));
    connect(mSplitProcess, SIGNAL(finished(int)), this, SLOT(slotSplitFinished(int)));

    mSplitProcess->start("split", args);
    mSplitWorking = mSplitProcess->waitForStarted();

    return mSplitWorking;
}

void MainWindow::slotSplitStarted()
{
    mSplitWorking = true;
}

void MainWindow::slotSplitFinished(int code)
{
    disconnect(mSplitProcess, SIGNAL(started()), this, SLOT(slotSplitStarted()));
    disconnect(mSplitProcess, SIGNAL(finished(int)), this, SLOT(slotSplitFinished(int)));

    mSplitRetCode = code;
    mSplitWorking = false;
}

void MainWindow::stopSplit(int msecWaitTime)
{
    if (!mSplitWorking)
        return;
    if (mSplitProcess->state() != QProcess::NotRunning)
    {
        mSplitProcess->terminate();
        int t = 0;
        while (true)
        {
            if (mSplitProcess->state() == QProcess::NotRunning)
                break;
            usleep(250 * 1000);
            t += 250;
            if (t >= msecWaitTime) // киляем
            {
                mSplitProcess->kill();
                break;
            }
        }
    }
    mSplitProcess->waitForFinished();
    delete mSplitProcess;
    mSplitProcess = NULL;
}

quint64 MainWindow::getFilesSizeInDir(QString dirName, QList<QString> &fileNames)
{
    QDir dir(dirName);
    QFileInfoList list = dir.entryInfoList();

    quint64 size = 0;

    for(int i = 0; i < list.size(); i++)
    {
        QFileInfo fileInfo = list.at(i);

        if(fileInfo.baseName().isEmpty())continue;

        if(fileInfo.isDir())
        {
            size += getFilesSizeInDir(fileInfo.absoluteFilePath(), fileNames);
        }
        else
        {
            size += fileInfo.size();
            fileNames.append(dirName + "/" + fileInfo.fileName());
        }
    }

    return size;
}

bool MainWindow::updateMetaFile()
{
    ByteBuffer metadataBuffer;

    if (readFileToBuffer(QString(mBaseDirName + Const::backupDir + Const::metaFile).toLocal8Bit().constData(), metadataBuffer) == false)
    {
        QMessageBox::warning(this, TR("Внимание! Запись невозможна"), TR("Ошибка открытия файла: %1").arg(Const::metaFile));
        return false;
    }

    std::string metadataJsonStr(metadataBuffer.data(), metadataBuffer.data() + metadataBuffer.size());
    Json::Reader reader;
    Json::Value  rootValue;
    if (!reader.parse(metadataJsonStr, rootValue))
    {
        QMessageBox::warning(this, TR("Внимание! Запись невозможна"), TR("Ошибка разбора файла: %1").arg(Const::metaFile));
        return false;
    }

    for (int i = 0; i < rootValue["devices"].size(); ++i)
    {
        for (int j = 0; j < rootValue["devices"][i]["partitions"][j].size(); ++j)
        {
            Json::Value partValue = rootValue["devices"][i]["partitions"][j];

            QString fileName = mBaseDirName + Const::backupDir + QString::fromStdString(partValue["name"].asString()) + ".tar.gz";
            QFile file(fileName);
            partValue["size"] = file.size();
            rootValue["devices"][i]["partitions"][j] = partValue;

            struct MainFileInfo mainFileInfo;

            mainFileInfo.fileName = QString::fromStdString(partValue["name"].asString()) + ".tar.gz";
            mainFileInfo.size = file.size();
            mainFileInfo.parts = 1;
            mainFileInfo.partsWritten = 0;
            mainFileInfo.partsForWrite = 0;

            mMainFilesInfo.append(mainFileInfo);
        }
    }

    std::ofstream metaFile;
    metaFile.open(QString(mBaseDirName + Const::backupDir + Const::metaFile).toLocal8Bit().constData());

    Json::StyledWriter writer;
    metaFile << writer.write(rootValue);
    metaFile.close();

    return true;
}

void MainWindow::deleteSplitFiles()
{
    QDir dir(mBaseDirName + Const::backupDir);
    QFileInfoList list = dir.entryInfoList();    

    for(int i = 0; i < list.size(); i++)
    {
        QFileInfo fileInfo = list.at(i);

        if(fileInfo.baseName().isEmpty()) continue;        
        else if(fileInfo.fileName().at(fileInfo.fileName().size() - 1).isDigit()) dir.remove(fileInfo.fileName());
    }
}

void MainWindow::updateFilesList(bool result)
{
    mFirstWrite = false;

    if(result)
    {
        for(int i = 0; i < mMainFilesInfo.size(); i++)
        {
            struct MainFileInfo &mainFileInfo = mMainFilesInfo[i];

            if(mainFileInfo.parts == mainFileInfo.partsWritten)continue;
            mainFileInfo.partsWritten = mainFileInfo.partsForWrite;
        }

        mWrittenSize += mSizeForWrite;
    }
    else
    {
        for(int i = 0; i < mMainFilesInfo.size(); i++)
        {
            struct MainFileInfo &mainFileInfo = mMainFilesInfo[i];

            if((mainFileInfo.partsWritten == 0) && (mainFileInfo.parts > 1))
            {
                for(int j = 0; j < mainFileInfo.parts; j++)
                {
                    QString fileName;
                    fileName.sprintf("%s.%02d", mainFileInfo.fileName.toLocal8Bit().constData(), j);

                    QFile file(mBaseDirName + Const::backupDir + fileName);
                    file.remove();
                }
            }

            mainFileInfo.partsForWrite = mainFileInfo.partsWritten;
        }

        if(mWrittenSize == 0)
            mFirstWrite = true;
    }
}

bool MainWindow::createExcludeFileName(QString excludeFileName)
{    
    qint64 leftSize;
    QList<QString> allFilesNames;
    QList<QString> filesForWrite;
    QList<QString> filesForExclude;

    mSizeForWrite = 0;

    //if(mAvailableDiscSpace < (remSize + MIN_ISO_SIZE))
    {
        if(mWrittenSize == 0)
        {
            QFile metaFile(mBaseDirName + Const::backupDir + Const::metaFile);

            mSizeForWrite = mTotalFilesSize - mMainFilesSize + metaFile.size();
            if(mAvailableDiscSpace < (mSizeForWrite + Const::minIsoSize))
            {
                QMessageBox::warning(this, TR("Внимание! Запись невозможна"),
                                     TR("Доступный размер свободного пространства на диске меньше допустимого: %1 байт").arg(Const::minIsoSize + mSizeForWrite));
                return false;
            }

            filesForWrite.append(Const::metaFile);
        }

        for(int i = 0; i < mMainFilesInfo.size(); i++)
        {
            struct MainFileInfo &mainFileInfo = mMainFilesInfo[i];

            leftSize = mAvailableDiscSpace - (mSizeForWrite + Const::minIsoSize);

            if(mainFileInfo.parts == mainFileInfo.partsWritten)continue;
            if(mainFileInfo.size <= leftSize)
            {
                if(mainFileInfo.parts == 1)
                {
                    filesForWrite.append(mainFileInfo.fileName);
                    mSizeForWrite += mainFileInfo.size;
                    mainFileInfo.partsForWrite++;
                }
                else
                {
                    for(int j = mainFileInfo.partsWritten; j < mainFileInfo.parts; j++)
                    {
                        QString fileName;
                        fileName.sprintf("%s.%02d", mainFileInfo.fileName.toLocal8Bit().constData(), j);
                        filesForWrite.append(fileName);

                        QFile file(mBaseDirName + Const::backupDir + fileName);
                        mSizeForWrite += file.size();
                        mainFileInfo.partsForWrite++;
                    }
                }
            }
            else
            {
                if(mainFileInfo.parts > 1)
                {
                    for(int j = mainFileInfo.partsWritten; j < mainFileInfo.parts; j++)
                    {
                        leftSize = mAvailableDiscSpace - (mSizeForWrite + Const::minIsoSize);

                        QString fileName;
                        fileName.sprintf("%s.%02d", mainFileInfo.fileName.toLocal8Bit().constData(), j);

                        QFile file(mBaseDirName + Const::backupDir + fileName);
                        if(file.size() > leftSize)
                        {
                            //записываем всё по порядку
                            goto exit;
                        }

                        filesForWrite.append(fileName);
                        mSizeForWrite += file.size();
                        mainFileInfo.partsForWrite++;
                    }
                }
                else
                {
                    QStringList args;
                    args << "-b" << QString::number(leftSize) << "-d" << mainFileInfo.fileName << (mainFileInfo.fileName + ".");

                    mProgBar->setVisible(true);
                    mProgBar->setFormat(TR("Разбиение файлов %p%"));
                    mProgBar->setValue(0);

                    if(runSplit(args))
                    {
                        while(mSplitWorking)
                        {
                            mSplitProcess->waitForFinished(10);
                            QCoreApplication::processEvents();
                        }
                    }

                    mProgBar->setVisible(false);

                    if(mSplitRetCode)
                    {
                        QMessageBox::warning(this, TR("Внимание!"),
                                             TR("Ошибка подготовки файлов для записи %1").arg(mSplitRetCode));
                        return false;
                    }

                    mainFileInfo.parts = mainFileInfo.size / leftSize + 1;

                    for(int j = mainFileInfo.partsWritten; j < mainFileInfo.parts; j++)
                    {
                        leftSize = mAvailableDiscSpace - (mSizeForWrite + Const::minIsoSize);

                        QString fileName;
                        fileName.sprintf("%s.%02d", mainFileInfo.fileName.toLocal8Bit().constData(), j);

                        QFile file(mBaseDirName + Const::backupDir + fileName);
                        if(file.size() > leftSize)
                        {
                            //записываем всё по порядку
                            goto exit;
                        }

                        filesForWrite.append(fileName);
                        mSizeForWrite += file.size();
                        mainFileInfo.partsForWrite++;
                    }
                }
            }
        }
    }
//    else
//    {
//        mSizeForWrite += remSize;
//    }

exit:

    getFilesSizeInDir(mBaseDirName, allFilesNames);
    for(quint16 i = 0; i < allFilesNames.size(); i++)
    {
        QString fileName = allFilesNames[i];

        if(fileName.contains(mBaseDirName + Const::backupDir))
        {
            bool isFound = false;
            for(quint16 j = 0; j < filesForWrite.size(); j++)
            {
                QString fileForWriteName = filesForWrite[j];
                if(fileName.compare(mBaseDirName + Const::backupDir + fileForWriteName) == 0)
                    isFound = true;
            }

            if(!isFound)
            {
                filesForExclude.append(fileName);
            }
        }
        else
        {
            if(!mFirstWrite)
                filesForExclude.append(fileName);
        }
    }

    QFile excludeFile(excludeFileName);
    if(excludeFile.open(QIODevice::WriteOnly))
    {
        QTextStream out(&excludeFile);

        for(int i = 0; i < filesForExclude.size(); i++)
        {
            out << filesForExclude.at(i) << "\n";
        }
        excludeFile.close();
    }
    else
    {
        QMessageBox::warning(this, TR("Внимание! Запись невозможна"),
                             TR("Ошибка создания файла %1").arg(excludeFileName));
        return false;
    }

    return true;
}

MainWindow::~MainWindow()
{
    // Завершение работы

    stopSplit();

    //! Останавливаем таймер для получения информации о диске
    if (mTimerUpdateDiscInfo)
    {
        mTimerUpdateDiscInfo->stop();
        delete mTimerUpdateDiscInfo;
        mTimerUpdateDiscInfo = 0;
    }

    //! Завершаем основной рабочий поток
    if (mDiscRecWorkingThread)
    {
        mDiscRecWorkingThread->shutdown();
        finishAndDeleteThread(mDiscRecWorkingThread, Const::msecWaitThreadTime);
    }

    delete ui;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    OperationType currOper = DiscRec::instance().currentOperation();

    switch (currOper)
    {
    case OPER_NO:
        QApplication::exit( EXIT_CANCEL );
        event->accept();
        break;

    case OPER_SCANNING_RECORDERS:
        QMessageBox::warning(this,
                             TR("Внимание! Выполняется операция поиска приводов"),
                             TR("Выполняется операция поиска приводов.\n"
                                "Прерывание операции невозможно!"));
        event->ignore();
        break;

    case OPER_CREATION_ISO:
        if (QMessageBox::question(this,
                                  TR("Внимание! Выполняется операция создания образа"),
                                  TR("Выполняется создание образа. В случае выхода диск записан не будет!\n"
                                     "Прервать создание образа и выйти?"),
                                  QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Ok)
        {
            DiscRec::instance().cancel();
            QApplication::exit( EXIT_CANCEL );
        }
        else
            event->ignore();
        break;

    case OPER_RECORDING_DISC:
        if (QMessageBox::question(this,
                                  TR("Внимание! Выполняется операция записи диска"),
                                  TR("Выполняется запись диска. В случае выхода диск будет записан неправильно!\n"
                                     "Операция не может быть сразу прервана.\n"
                                     "Попытаться прервать запись диска и выйти?"),
                                  QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Ok)
        {
            //! Останавливаем таймер для получения информации о диске
            if (mTimerUpdateDiscInfo)
            {
                mTimerUpdateDiscInfo->stop();
                delete mTimerUpdateDiscInfo;
                mTimerUpdateDiscInfo = 0;
            }

            DiscRec::instance().cancelAndWait();
            QApplication::exit( EXIT_CANCEL );
            event->accept();


        }
        else
            event->ignore();
        break;

    case OPER_ERASING_DISC:
        QMessageBox::warning(this,
                             TR("Внимание! Выполняется операция стирания диска"),
                             TR("Выполняется стирание диска.\n"
                                "Прерывание операции невозможно!"));
        event->ignore();
        break;

    case OPER_FORMATTING_DISC:
        QMessageBox::warning(this,
                             TR("Внимание! Выполняется операция форматирования диска"),
                             TR("Выполняется форматирование диска.\n"
                                "Прерывание операции невозможно!"));
        event->ignore();
        break;

    case OPER_CANCELLING:
        QMessageBox::warning(this,
                             TR("Внимание! Выполняется отмена операции"),
                             TR("Выполняется прерывание операции.\n"
                                "Прерывание операции невозможно!"));
        event->ignore();
        break;
    case OPER_INITIALIZING:
        QMessageBox::warning(this,
                             TR("Внимание! Выполняется инициализация библиотек"),
                             TR("Выполняется инициализация библиотек.\n"
                                "Прерывание операции невозможно!"));
        event->ignore();
        break;
    case OPER_SHUTDOWNING:
        QMessageBox::warning(this,
                             TR("Внимание! Выполняется завершение работы с библиотеками"),
                             TR("Выполняется завершение работы с библиотеками.\n"
                                "Прерывание операции невозможно!"));
        event->ignore();
        break;
    case OPER_DRIVE_INFO:
    case OPER_DISC_INFO:
        if (QMessageBox::question(this,
                             TR("Внимание! Завершение не рекомендуется"),
                             TR("Выполняется запрос информации о приводе и диске.\n"
                                "Рекомендуется дождаться завершения данного процесса.\n"
                                "Вы действительно хотите выйти?"),
                             QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Ok)
        {
            //! Останавливаем таймер для получения информации о диске
            if (mTimerUpdateDiscInfo)
            {
                mTimerUpdateDiscInfo->stop();
                delete mTimerUpdateDiscInfo;
                mTimerUpdateDiscInfo = 0;
            }

            DiscRec::instance().cancel();
            QApplication::exit( EXIT_CANCEL );
            event->accept();
        }
        else
            event->ignore();
        break;

    default:
        //! По дефолту выходим
        QApplication::exit( EXIT_CANCEL );
        event->accept();
    }
}

void MainWindow::checkDisc()
{
    //! Запомним последнюю успешно выполненную операцию
    OperationType prevSuccessOperation = mLastSuccessOperation;

    //! Если вдруг придет нежданная информация о диске и приводе пометим как OPER_NO - чтобы ниженаписанное не сработало повторно
    mLastSuccessOperation = OPER_NO;

    if (mDiscType == -100)
    {
        ui->statusBar->showMessage(TR("Ошибка записи диска"));
        QMessageBox::warning(this, TR("Внимание! Запись невозможна"), TR("Ошибка при получении типа диска, либо диска нет"));
        goto maybe_delete_iso_and_quit;
    }

    //! Проверим не вставлен ли диск "только для чтения"
    if (mDiscType == 0x08 || mDiscTypeName == "CD-ROM" ||
        mDiscType == 0x10 || mDiscTypeName == "DVD-ROM" ||
        mDiscType == 0x40 || mDiscTypeName == "BD-ROM" || mDiscType == 0x42 || mDiscTypeName == "BD-R random recording")
    {
        //! Диск для записи не предназначен
        ui->statusBar->showMessage(TR("Ошибка записи диска"));
        QMessageBox::warning(this, TR("Внимание! Запись невозможна"), TR("Запись на данный тип диска невозможна (только для чтения)"));
        goto maybe_delete_iso_and_quit;
    }

    //! Диск может быть новым, надо проверить отформатирован ли
    if (mDiscType == 0x1a || mDiscTypeName == "DVD+RW" || mDiscType == 0x12 || mDiscTypeName == "DVD-RAM" || // без 0x1a || DVD+RW тоже работает, новый DVD+RW можно не форматить
        mDiscType == 0x43 || mDiscTypeName == "BD-RE" || mDiscType == 0x41 || mDiscTypeName == "BD-R sequential recording") // BD-R поидее сразу можно писать
    {
        if (mFormatStatus == BURN_FORMAT_IS_UNFORMATTED)
        {
            // диск не отформатирован
            if (QMessageBox::question(this,
                                      TR("Необходимо форматирование диска"),
                                      TR("Диск не отформатирован. Для записи необходимо форматирование.\nВыполнить форматирование?"),
                                      QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok) == QMessageBox::Ok)
            {
                lockGUIRecordStart();
                //! Выполнение форматирования диска
                mDiscRecWorkingThread->formatDisc(ui->coBoxRecorder->currentText());
                return;
            }
            else
            {
                ui->statusBar->showMessage(TR("Ошибка записи диска"));
                goto maybe_delete_iso_and_quit;
            }
        }
    }

    //! Диск записываемый и его, возможно, надо стереть (либо предложить отформатировать "DVD-RW sequential recording" в более лучший формат "DVD-RW restricted overwrite")
    if (mDiscType == 0x0a || mDiscTypeName == "CD-RW" ||
        mDiscType == 0x12 || mDiscTypeName == "DVD-RAM" || mDiscType == 0x14 || mDiscTypeName == "DVD-RW sequential recording" ||
        mDiscType == 0x43 || mDiscTypeName == "BD-RE")
    {
        // Warning! DVD-RW restricted overwrite сразу пишется, без стирания! DVD-RW sequential recording только после стирания или форматирования в DVD-RW restricted overwrite

        //! Спросим о форматировании DVD-RW sequential recording в DVD-RW restricted overwrite в случае если предыущая операция не была стиранием (т.е. отказом от форматирования)
        if ((mDiscType == 0x14 || mDiscTypeName == "DVD-RW sequential recording") && prevSuccessOperation != OPER_ERASING_DISC)
        {
            if (QMessageBox::question(this,
                                      TR("Возможно форматирование диска"),
                                      TR("В данный момент диск имеет формат последовательной записи.\n"
                                         "Его можно отформатировать в более лучший формат ограниченной перезаписи.\n"
                                         "Если не выполнять форматирование, при каждой записи диска его необходимо стирать.\n"
                                         "Выполнить форматирование в DVD-RW restricted overwrite?"),
                                      QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok) == QMessageBox::Ok)
            {
                lockGUIRecordStart();
                //! Выполнение форматирования диска (благоразумный пользователь =) )
                mDiscRecWorkingThread->formatDisc(ui->coBoxRecorder->currentText());
                return;
            }
            else
            {
                // ну пусть еще ответит и на следующий вопрос о стирании если диск не чистый...
            }
        }
        if (mDiscStatus == BURN_DISC_FULL || mDiscStatus == BURN_DISC_APPENDABLE)
        {
            if (mIsDiscErasable)
            {
                // на диске что-то есть, спросим снести ли
                if (QMessageBox::question(this,
                                          TR("Необходима очистка диска"),
                                          TR("Для записи диска необходимо его очистить. Выполнить стирание?"),
                                          QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok) == QMessageBox::Ok)
                {
                    lockGUIRecordStart();
                    //! Выполнение стирания диска
                    mDiscRecWorkingThread->eraseDisc(ui->coBoxRecorder->currentText());
                    return;
                }
                else
                {
                    ui->statusBar->showMessage(TR("Ошибка записи диска"));
                    goto maybe_delete_iso_and_quit;
                }
            }
            else
            {
                QMessageBox::warning(this, TR("Диск не пригоден для записи"), TR("Очистка диска не возможна. Запись отменена."));
                ui->statusBar->showMessage(TR("Ошибка записи диска"));
            }
        }
        else if (mDiscStatus == BURN_DISC_BLANK)
        {
            if(prevSuccessOperation != OPER_CREATION_ISO)
            {
                QString filesPath = ui->leFilesPath->text();

                if(createExcludeFileName(Const::excludeFile))
                    mDiscRecWorkingThread->makeISO(Const::discTitle, filesPath, Const::isoPath, Const::excludeFile, mFirstWrite);
            }
            else
            {
                //! Выполнение записи диска
                makeRecord(mAvailableDiscSpace);
            }
            return;
        }
    }
    else // однозаписываемые и DVD-RW restricted overwrite стирать не надо
    {
        if(prevSuccessOperation != OPER_CREATION_ISO)
        {
            QString filesPath = ui->leFilesPath->text();

            if(createExcludeFileName(Const::excludeFile))
                mDiscRecWorkingThread->makeISO(Const::discTitle, filesPath, Const::isoPath, Const::excludeFile, mFirstWrite);
        }
        else
        {
            //! Выполнение записи диска
            makeRecord(mAvailableDiscSpace);
        }
        return;
    }
    unlockGUIRecordFinish();
    return;

maybe_delete_iso_and_quit:

//    QFile::remove(Const::isoPath);
    unlockGUIRecordFinish();
}

void MainWindow::slotReceiveDriveInfo(DriveInfoConst driveInfo)
{
    if (!ui->coBoxRecorder->currentText().size())
    {
        ui->teRecDiscInfo->setText(TR("Не выбран привод для запроса информации..."));
        unlockGUIRecordFinish();
        return;
    }

    //! Обновляем члены класса с информацией
    mDriveInfoLock.lockForWrite();
    mDriveInfo = driveInfo;
    mDriveInfoLock.unlock();

    if (!updateDriveInfo())
    {
        mIsDevFileSuccessOpened = false;
    }

}

void MainWindow::slotReceiveDiscInfo(DiscInfo discInfo)
{
    if (!ui->coBoxRecorder->currentText().size())
    {
        ui->teRecDiscInfo->setText(TR("Не выбран привод для запроса информации..."));
        unlockGUIRecordFinish();
        return;
    }

    //! Обновляем члены класса с информацией
    mDiscInfoLock.lockForWrite();
    mDiscInfo = discInfo;
    mDiscInfoLock.unlock();

    if (!updateDiscInfo())
    {
        mIsDevFileSuccessOpened = false;
        return;
    }

    mDiscIsChanged = false;

    //! Тип диска
    mDiscType = -100;
    mDiscTypeName = TR("-");
    discInfo.getDiscType(mDiscType, mDiscTypeName);

    //! Статус диска
    mDiscStatus = discInfo.getDiscStatus();
    QString discStatusName =
            (mDiscStatus == BURN_DISC_UNREADY) ? TR("неизвестен") :
            (mDiscStatus == BURN_DISC_BLANK)? TR("чистый") :
            (mDiscStatus == BURN_DISC_EMPTY)? TR("-") :
            (mDiscStatus == BURN_DISC_APPENDABLE)? TR("записанный и не закрытый") :
            (mDiscStatus == BURN_DISC_FULL)? TR("записанный и закрытый") :
            (mDiscStatus == BURN_DISC_UNGRABBED)? TR("ошибка (устройство не захвачено)") :
            (mDiscStatus == BURN_DISC_UNSUITABLE)? TR("неподходящий для чтения/записи") : TR("ошибка");

    //! Статус форматирования
    mFormatStatus = -1;
    int numFormats = -1;
    off_t formatSize = -1;
    discInfo.getDiscFormatType(mFormatStatus, numFormats, formatSize);

    QString formatStatusName =
            (mDiscStatus == BURN_DISC_EMPTY) ? TR("-") :
            (mFormatStatus == BURN_FORMAT_IS_FORMATTED) ? TR("отформатированный") :
            (mFormatStatus == BURN_FORMAT_IS_UNFORMATTED) ? TR("неотформатированный") :
            (mFormatStatus == BURN_FORMAT_IS_UNKNOWN) ? ((mDiscType == 0x13 || mDiscType == 0x14 || mDiscTypeName == "DVD-RW restricted overwrite" || mDiscTypeName == "DVD-RW sequential recording") ? TR("быстро отформат.") : TR("неизвестный")) : TR("ошибка");


    //! Стираемый ли диск
    mIsDiscErasable = discInfo.isDiscErasable();

    //! Свободное пространство для записи
    mAvailableDiscSpace = discInfo.getAvailableDiscSpace();

    //! Выполняем операции с диском только если последними успешными выполненными операциями были стирание/форматирование/создание образа
    if (mLastSuccessOperation == OPER_ERASING_DISC || mLastSuccessOperation == OPER_FORMATTING_DISC
            || mLastSuccessOperation == OPER_CREATION_ISO)
    {
        checkDisc();
    }
}

void MainWindow::slotRequestDriveInfo()
{
    if (!ui->coBoxRecorder->currentText().size())
    {
        QMessageBox::warning(this, TR("Ошибка"), TR("Ошибка запроса на получение информации о приводе.\nПривод не выбран"));
        return;
    }
    ui->teRecDiscInfo->clear();
    ui->teRecDiscInfo->setText(TR("Выполняется запрос информации о приводе. Подождите..."));
    lockGUIRecordStart();
    mDiscRecWorkingThread->getDriveInfo(ui->coBoxRecorder->currentText());
}

void MainWindow::slotRequestDiscInfo()
{
    if (!ui->coBoxRecorder->currentText().size())
    {
        QMessageBox::warning(this, TR("Ошибка"), TR("Ошибка запроса на получение информации о диске.\nПривод не выбран"));
        return;
    }
    ui->teRecDiscInfo->clear();
    ui->teRecDiscInfo->setText(mDriveInfoStr + "\n\n" + TR("Выполняется запрос информации о диске. Подождите..."));
    lockGUIRecordStart();
    mDiscRecWorkingThread->getDiscInfo(ui->coBoxRecorder->currentText());
}
void MainWindow::slotScanRecorders()
{
    lockGUIRecordStart();
    mDiscRecWorkingThread->scanDrives();
}

void MainWindow::finishAndDeleteThread(QThread* thread, int msecWaitTime)
{
    if (!thread)
        return;
    if (thread->isRunning())
    {
        thread->quit();
        if (!thread->wait())    // пытаемся дождаться завершения
            thread->terminate();            // очень грубо завершаем
    }
    delete thread;
    thread = 0;
}

void MainWindow::finishAndDeleteProcess(QProcess* process, int msecWaitTime)
{
    if (!process)
        return;
    if (process->state() != QProcess::NotRunning)
    {
        process->terminate();
        int t = 0;
        while (true)
        {
            if (process->state() == QProcess::NotRunning)
                break;
            usleep(250 * 1000);
            t += 250;
            if (t >= msecWaitTime) // киляем
            {
                process->kill();
                break;
            }
        }
    }
    delete process;
    process = 0;
}

void MainWindow::setGUIState(bool state)
{
    ui->leFilesPath->setEnabled(state);
    ui->btnSelectPath->setEnabled(state);
    ui->coBoxRecorder->setEnabled(state);
    ui->btnScanRecorders->setEnabled(state);
    ui->btnRecord->setEnabled(state);
    ui->btnCancel->setEnabled(!state);
}

void MainWindow::lockGUIRecordStart()
{
    setGUIState(false);
}

void MainWindow::unlockGUIRecordFinish()
{
    setGUIState(true);
    slotFilesPathTextChanged(ui->leFilesPath->text());
}

void MainWindow::makeRecord(off_t availableDiscSpace)
{
    //! Получим размер файл-образа и проверим доступен ли для чтения
    qint64 isoSize = -1;
    QFile file(Const::isoPath);
    if (file.open(QIODevice::ReadOnly))
    {
        isoSize = file.size();
        file.close();

        //! Проверим хватает ли места на диске для записи образа
        if (isoSize > availableDiscSpace)
        {
            ui->statusBar->showMessage(TR("Ошибка записи диска"));
            QMessageBox::warning(this, TR("Внимание! Запись невозможна"), TR("Диск записать невозможно, т.к. на нем недостаточно места для записи образа"));
        }
        else
        {
//#
//            if (QMessageBox::question(this,
//                                      TR("Создание образа успешно выполнено"),
//                                      TR("Образ на запись подготовлен.\nЗаписать его на компакт-диск?"),
//                                      QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok) == QMessageBox::Ok)
//            {
                //! Выполнение записи диска
                lockGUIRecordStart();
                mDiscRecWorkingThread->recordISO(ui->coBoxRecorder->currentText(), Const::isoPath);
                return;
//            }
//            else
//            {
//                ui->statusBar->showMessage(TR("Запись диска не была подтверждена"));
//            }
        }
    }
    else
    {
        ui->statusBar->showMessage(TR("Ошибка записи диска"));
        QMessageBox::warning(this, TR("Внимание! Запись невозможна"), TR("Ошибка открытия файл-образа"));
    }
//#
//    if (QMessageBox::question(this,
//                              TR("Удалить файл-образ"), TR("Вы желаете удалить созданный файл-образ?"),
//                              QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok) == QMessageBox::Ok)
    //QFile::remove(Const::isoPath);
    unlockGUIRecordFinish();
}

void MainWindow::slotRecorderChanged(int /*index*/)
{
    slotRequestDriveInfo();
}

void MainWindow::slotSelectPath()
{
/*    QString path = QFileDialog::getExistingDirectory(this, TR("Выберите директорию с файлами для записи"), ui->leFilesPath->text());
    if (!path.isEmpty())*/
        ui->leFilesPath->setText("/");
}

void MainWindow::slotFilesPathTextChanged(const QString& /*text*/)
{
    QDir dir(ui->leFilesPath->text());
    bool isCorrectPaths = !dir.exists() || !ui->coBoxRecorder->currentText().size();

    if (isCorrectPaths)
    {
//        ui->statusBar->showMessage(TR("Выберите привод, либо каталог на запись"));
        ui->statusBar->showMessage(TR("Запись невозможна"));                            // Такого быть не должно!
        ui->btnRecord->setEnabled(false);
        ui->btnCancel->setEnabled(true);
    }
    else
    {
//        ui->statusBar->showMessage(TR("Привод и каталог на запись выбраны"));
        ui->statusBar->showMessage(TR("Данные готовы к записи"));
        ui->btnRecord->setEnabled(true);
        ui->btnCancel->setEnabled(true);
    }
}

void MainWindow::slotRecord()
{
    lockGUIRecordStart();

    QFile(Const::isoPath).remove();

    checkDisc();
//    if(createExcludeFileName(Const::excludeFile))
//        mDiscRecWorkingThread->makeISO(Const::discTitle, filesPath, Const::isoPath);
    // Дальше вся работа в slotOperationFinished(), slotReceiveDriveDiscInfo() и makeRecord()
}

void MainWindow::slotCancel()
{
    //! Прерывание текущей работы
    DiscRec::instance().cancel();
    QApplication::exit( EXIT_CANCEL );

}

void MainWindow::slotReceiveMessage(MessageType mstType, QString text)
{
    /*
    qDebug(TR("Receive %s message from %s: %s").toLocal8Bit().constData(),
           (mstType == MSG_INFORMATION) ? "information" :
           (mstType == MSG_WARNING) ? "warning" :
           (mstType == MSG_CRITICAL) ? "critical" :
           (mstType == MSG_EXECUTION_PERCENT) ? "execution percent" :
           (mstType == MSG_CURRENT_OPERATION) ? "current operation" : "unknown",
           sender()->objectName().toLocal8Bit().constData(),
           (text.size()) ? text.toLocal8Bit().constData() : (mstType == MSG_CURRENT_OPERATION) ? "operation complete" : "");
    */
    if (mstType == MSG_EXECUTION_PERCENT)
    {
        mProgBar->setVisible(true);
        mProgBar->setValue(text.toInt());
    }
    else if (mstType == MSG_CURRENT_OPERATION)
    {
        if (text.size())
        {
            mProgBar->setVisible(true);
            mProgBar->setFormat(TR("%1 %p%").arg(text));
            ui->statusBar->showMessage(text);
        }
        else
        {
            mProgBar->setVisible(false);
            ui->statusBar->showMessage(TR("Операция завершена"));
        }
    }
    else if (mstType == MSG_INFORMATION)
        QMessageBox::information(this, TR("Информационное сообщение"), text);
    else if (mstType == MSG_WARNING)
        QMessageBox::information(this, TR("Внимание!"), text);
    else if (mstType == MSG_CRITICAL)
    {
        QMessageBox::information(this, TR("Критическая ошибка!"), text);
        QApplication::exit( EXIT_ERROR );
    }
}

void MainWindow::slotOperationFinished(OperationType finishedOperation, bool result)
{
    //! Запомним последнюю успешно выполненную операцию
    OperationType prevSuccessOperation = mLastSuccessOperation;

    /*
    //! Последняя успешно выполненная операция пока считается как OPER_NO
    if (finishedOperation != OPER_DISC_DRIVE_INFO)
        mLastSuccessOperation = OPER_NO;
    */

    mLastOperationResult = result;

    //! Обновим последнюю успешно выполненную операцию
    if (result && finishedOperation != OPER_DISC_INFO && finishedOperation != OPER_DRIVE_INFO)
        mLastSuccessOperation = finishedOperation;

    //! Временно, все-равно перетрется ниже
    ui->statusBar->showMessage(TR("Операция завершена %2").arg((result) ? TR("успешно") : TR("с ошибкой")));

    if (finishedOperation == OPER_SCANNING_RECORDERS) // завершено сканирование приводов
    {
        disconnect(ui->coBoxRecorder,
                SIGNAL(currentIndexChanged(int)),
                this,
                SLOT(slotRecorderChanged(int)));
        ui->coBoxRecorder->clear();
        connect(ui->coBoxRecorder,
                SIGNAL(currentIndexChanged(int)),
                SLOT(slotRecorderChanged(int)));

        if (result)
        {
            ui->coBoxRecorder->addItems(DiscRec::instance().drivesList()); // запросит инфомацию о диске и приводе
            ui->statusBar->showMessage(TR("Поиск устройств завершен"));
        }
        else
            ui->statusBar->showMessage(TR("Ошибка поиска устройств"));
    }
    else if (finishedOperation == OPER_CREATION_ISO) // завершена операция создания образа
    {
        unlockGUIRecordFinish();
        if (result)
        {
            ui->statusBar->showMessage(TR("Образ на запись создан"));

            if(mDiscIsChanged)
                slotRequestDiscInfo();
            else
                checkDisc();
        }
        else
        {
            ui->statusBar->showMessage(TR("Ошибка создания образа"));
            goto maybe_delete_iso_and_quit;
        }
    }
    else if (finishedOperation == OPER_ERASING_DISC)
    {
        unlockGUIRecordFinish();

        //! Запросим заново информацию о диске и приводе
        slotRequestDiscInfo();

        if (result)
            ui->statusBar->showMessage(TR("Стирание диска завершено"));
        else
        {            
            ui->statusBar->showMessage(TR("Ошибка стирания диска"));            
            goto maybe_delete_iso_and_quit;
        }
    }
    else if (finishedOperation == OPER_FORMATTING_DISC)
    {
        unlockGUIRecordFinish();

        //! Запросим заново информацию о диске и приводе
        slotRequestDiscInfo();

        if (result)
            ui->statusBar->showMessage(TR("Форматирование диска завершено"));
        else
        {
            ui->statusBar->showMessage(TR("Ошибка форматирования диска"));            
            goto maybe_delete_iso_and_quit;
        }
    }
    else if (finishedOperation == OPER_RECORDING_DISC)
    {
        unlockGUIRecordFinish();

        if (result)
        {
            updateFilesList(true);
            showRecordedInfo();
            ui->statusBar->showMessage(TR("Запись диска завершена"));

            //! Запросим заново информацию о диске и приводе
            if(mWrittenSize == mTotalFilesSize)
            {
                if(mDiscNumber == 1)
                {
                    QMessageBox::information(this,"Запись диска","Запись успешно завершена");
                }
                else
                {
                    QMessageBox::information(this,"Запись диска",QString("Запись %1-го диска успешно завершена").arg(mDiscNumber));
                }
                lockGUIRecordStart();
                QFile::remove(Const::isoPath);
                goto quit;
            }
            else
            {
                QMessageBox::information(this,"Запись диска",QString("Запись %1-го диска успешно завершена").arg(mDiscNumber));
                QMessageBox::information(this,"Запись диска","Вставьте следующий диск");
            }
            mDiscNumber++;
        }
        else
        {
            ui->statusBar->showMessage(TR("Ошибка записи диска"));
            //! не работает захват после burn_drive_cancel()!!! решение - перескан устройств, а не запрос информации о старом приводе...
            slotScanRecorders();
        }
        goto maybe_delete_iso_and_quit;
    }
    else if (finishedOperation == OPER_CANCELLING)
    {
        unlockGUIRecordFinish();

        //! Запросим заново информацию о диске и приводе
        slotRequestDiscInfo();

        if (result)
            ui->statusBar->showMessage(TR("Отмена операции выполнена"));
        else
            ui->statusBar->showMessage(TR("Ошибка отмены операции"));

        //! Если образ был создан предложим все-таки его удалить
        if (prevSuccessOperation == OPER_CREATION_ISO || prevSuccessOperation == OPER_RECORDING_DISC || prevSuccessOperation == OPER_ERASING_DISC || prevSuccessOperation == OPER_FORMATTING_DISC)
            goto maybe_delete_iso_and_quit;
    }
    else if (finishedOperation == OPER_INITIALIZING)
    {
        ui->centralWidget->setEnabled(result);
        if (result)
            ui->statusBar->showMessage(TR("Выберите каталог на запись"));
        else
            ui->statusBar->showMessage(TR("Ошибка инициализации"));
    }
    else if (finishedOperation == OPER_SHUTDOWNING)
    {
        ui->centralWidget->setEnabled(result);
        if (result)
            ui->statusBar->showMessage(TR("Завершение работы с библ. выполнено"));
        else
            ui->statusBar->showMessage(TR("Ошибка завершения работы с библ."));
    }
    else if (finishedOperation == OPER_DRIVE_INFO)
    {
        if (result)
        {
            ui->statusBar->showMessage(TR("Информация о приводе получена"));
            unlockGUIRecordFinish();
        }
        else
        {
            ui->statusBar->showMessage(TR("Ошибка получения инф. о приводе"));
            ui->teRecDiscInfo->clear();
            ui->teRecDiscInfo->setText(TR("Ошибка получения информации о приводе.\nВыполните повторно поиск устройств..."));
        }
    }
    else if (finishedOperation == OPER_DISC_INFO)
    {
        if (result)
        {
            ui->statusBar->showMessage(TR("Информация о диске получена"));
            unlockGUIRecordFinish();
        }
        else
        {
            if(++mDiscInfoTryCounts > 8)
            {
                ui->statusBar->showMessage(TR("Ошибка получения инф. о диске"));
                ui->teRecDiscInfo->clear();
                ui->teRecDiscInfo->setText(mDriveInfoStr + "\n\n" + TR("Ошибка получения информации о диске"));
            }
        }
    }

quit:
    mProgBar->setVisible(false);



//    if( finishedOperation == OPER_RECORDING_DISC )
//    {
//        if( result )
//            QApplication::exit( EXIT_NORMAL );
//        else
//            QApplication::exit( EXIT_ERROR );
//    } else if( finishedOperation == OPER_CANCELLING )
//    {
//        QApplication::exit( EXIT_CANCEL );
//    } else if( finishedOperation == OPER_NO )
//    {
//        QApplication::exit( EXIT_CANCEL );
//    }


    return;

maybe_delete_iso_and_quit:

    if(!result) updateFilesList(false);
//#
//    if (QMessageBox::question(this,
//                              TR("Удалить файл-образ"), TR("Вы желаете удалить созданный файл-образ?"),
//                              QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok) == QMessageBox::Ok)
    QFile::remove(Const::isoPath);
    unlockGUIRecordFinish();

    goto quit;
}

bool MainWindow::updateDriveInfo(bool overwrite)
{
    mDriveInfoLock.lockForRead();

    if (ui->coBoxRecorder->currentText() != mDriveInfo.driveName())
    {
        ui->statusBar->showMessage(TR("Ошибка получения инф. о приводе"));
        mDriveInfoStr = TR("Дождитесь повторного запроса информации о приводе...");
        ui->teRecDiscInfo->setText((overwrite) ? mDriveInfoStr : ui->teRecDiscInfo->toPlainText() + "\n\n" + mDriveInfoStr);
        mDriveInfoLock.unlock();
        return false;
    }

    mDriveInfoStr =
            TR( "*** Информация о приводе ***\n"
                "Вендор: %1 | Модель: %2 | Ревизия: %3 | Местоположение: %4\n"
                "DVD-RAM: %5 | DVD+R: %6 | DVD-R: %7\n"
                "DVD+RW: %8 | DVD-RW: %9\n"
                "CD-R: %10 | CD-RW: %11\n"
                "BD-R: %12")
            .arg(mDriveInfo.driveVendor()).arg(mDriveInfo.driveProduct()).arg(mDriveInfo.driveRevision()).arg(mDriveInfo.driveLocation())
            .arg((mDriveInfo.driveIsDvdRam()) ? "+" : "-").arg((mDriveInfo.driveIsDvdRPlus()) ? "+" : "-").arg((mDriveInfo.driveIsDvdRMinus()) ? "+" : "-")
            .arg((mDriveInfo.driveIsDvdRWMinus()) ? "+" : "-").arg((mDriveInfo.driveIsDvdRWPlus()) ? "+" : "-")
            .arg((mDriveInfo.driveIsCdR()) ? "+" : "-").arg((mDriveInfo.driveIsCdRW()) ? "+" : "-")
            .arg((mDriveInfo.driveIsBdR()) ? "+" : "-");

    mDriveInfoLock.unlock();

    ui->teRecDiscInfo->setText((overwrite) ? mDriveInfoStr : ui->teRecDiscInfo->toPlainText() + "\n\n" + mDriveInfoStr);
    return true;
}

bool MainWindow::updateDiscInfo(bool overwrite)
{
    //! Строка с информацией о диске
    QString discInfoStr;

    mDiscInfoLock.lockForRead();

    if (ui->coBoxRecorder->currentText() != mDiscInfo.driveName())
    {
        ui->statusBar->showMessage(TR("Ошибка получения инф. о диске"));
        discInfoStr = TR("Дождитесь повторного запроса информации о диске...");
        ui->teRecDiscInfo->setText(mDriveInfoStr + "\n\n" + discInfoStr);
        mDiscInfoLock.unlock();
        return false;
    }
    if (!mDiscInfo.isValid())
    {
        QString coBoxRecStr = ui->coBoxRecorder->currentText();
        int fstDevIdx = coBoxRecStr.indexOf("(dev='") + QString("(dev='").size();
        int sndDevIdx = coBoxRecStr.indexOf("')");
        QString devStr = coBoxRecStr.mid(fstDevIdx, sndDevIdx - fstDevIdx);
        int fd;

        if((fd = ::open(devStr.toStdString().c_str(), O_RDONLY|O_NONBLOCK)) != -1)
        {
            int result=ioctl(fd, CDROM_DRIVE_STATUS, CDSL_NONE);
            ::close(fd);

            if(result == CDS_NO_DISC)
                discInfoStr = TR("Нет диска");
            else if(result == CDS_TRAY_OPEN)
                discInfoStr = TR("Лоток открыт");
            else if(result == CDS_NO_INFO)
                discInfoStr = TR("Нет информации о диске");
        }

        if(discInfoStr.size() == 0)
        {
            if(++mDiscInfoTryCounts > 8)
                discInfoStr = TR("Ошибка получения информации о диске");
        }
        if(discInfoStr.size() != 0)
        {
            ui->statusBar->showMessage(TR("Ошибка получения инф. о диске"));
            ui->teRecDiscInfo->setText(mDriveInfoStr + "\n\n" + discInfoStr);
        }

        mDiscInfoLock.unlock();
        return false;
    }

    mDiscInfoTryCounts = 0;

    //! Тип диска
    int discType = -100;
    QString discTypeName(TR("-"));
    if (mDiscInfo.getDiscType(discType, discTypeName))
    {
        // успешно
        mIsDevFileSuccessOpened = true;
    }
    else
    {
        // ошибка
        mIsDevFileSuccessOpened = false;
    }

    //! Статус диска
    burn_disc_status discStatus = mDiscInfo.getDiscStatus();
    QString discStatusName =
            (discStatus == BURN_DISC_UNREADY) ? TR("неизвестен") :
            (discStatus == BURN_DISC_BLANK)? TR("чистый") :
            (discStatus == BURN_DISC_EMPTY)? TR("-") :
            (discStatus == BURN_DISC_APPENDABLE)? TR("записанный и не закрытый") :
            (discStatus == BURN_DISC_FULL)? TR("записанный и закрытый") :
            (discStatus == BURN_DISC_UNGRABBED)? TR("ошибка (устройство не захвачено)") :
            (discStatus == BURN_DISC_UNSUITABLE)? TR("неподходящий для чтения/записи") : TR("ошибка");

    //! Статус форматирования
    int formatStatus = -1;
    int numFormats = -1;
    off_t formatSize = -1;
    if (mDiscInfo.getDiscFormatType(formatStatus, numFormats, formatSize))
    {
        // успешно
    }
    else
    {
        // ошибка
    }

    QString formatStatusName =
            (discStatus == BURN_DISC_EMPTY) ? TR("-") :
            (formatStatus == BURN_FORMAT_IS_FORMATTED) ? TR("отформатированный") :
            (formatStatus == BURN_FORMAT_IS_UNFORMATTED) ? TR("неотформатированный") :
            (formatStatus == BURN_FORMAT_IS_UNKNOWN) ? ((discType == 0x13 || discType == 0x14 || discTypeName == "DVD-RW restricted overwrite" || discTypeName == "DVD-RW sequential recording") ? TR("быстро отформат.") : TR("неизвестный")) : TR("ошибка");

    //! Статус устройства
    //burn_progress burnProgress;
    //burn_drive_status driveStatus = discInfo.getDriveStatus(&burnProgress);

    //! Стираемый ли диск
    bool isDiscErasable = mDiscInfo.isDiscErasable();

    //! Скорости работы с диском
//    int maxWriteSpeed = mDiscInfo.getMaxWriteSpeed();
//    int minWriteSpeed = mDiscInfo.getMinWriteSpeed();
//    int maxReadSpeed = mDiscInfo.getMaxReadSpeed();

    //! Свободное пространство для записи
    off_t availableDiscSpace = mDiscInfo.getAvailableDiscSpace();

    mDiscInfoLock.unlock();

    discInfoStr =
            TR( "*** Информация о диске ***\n"
                "Тип диска: %1 | Статус диска: %2 | Диск стираемый: %3\n"
                "Статус формат.: %4 | Кол-во доступных форм.: %5\n"
                //"Макс. ск. зап.: %6 КБ/с | Мин. ск. зап.: %7 КБ/с | Макс. ск. чт.: %8 КБ/с\n"
                "Доступный размер для записи: %6 Мбайт (%7 Гбайт)\n" )
            .arg(discTypeName).arg(discStatusName).arg((isDiscErasable) ? "+" : "-")
            .arg(formatStatusName).arg(QString::number(numFormats))
            //.arg(QString::number((double)maxWriteSpeed * 1000 / 1024, 'f', 2)).arg(QString::number((double)minWriteSpeed * 1000 / 1024, 'f', 2)).arg(QString::number((double)maxReadSpeed * 1000 / 1024, 'f', 2))
            .arg(QString::number((double)availableDiscSpace / 1024 / 1024, 'f', 2)).arg(QString::number((double)availableDiscSpace / 1024 / 1024 / 1024, 'f', 2));

    ui->teRecDiscInfo->setText(mDriveInfoStr + "\n\n" + discInfoStr);
    return true;
}

void MainWindow::updateDiscDriveInfoFileCheck()
{
    //qDebug() << "updateDiscDriveInfoFileCheck" << DiscRec::instance().currentOperation() ;

    if (DiscRec::instance().currentOperation() == OPER_DRIVE_INFO ||
        DiscRec::instance().currentOperation() == OPER_DISC_INFO ||
        DiscRec::instance().currentOperation() == OPER_SCANNING_RECORDERS ||
        DiscRec::instance().currentOperation() == OPER_RECORDING_DISC ||
        DiscRec::instance().currentOperation() == OPER_ERASING_DISC ||
        DiscRec::instance().currentOperation() == OPER_FORMATTING_DISC ||
        DiscRec::instance().currentOperation() == OPER_CANCELLING ||
        DiscRec::instance().currentOperation() == OPER_INITIALIZING ||
        DiscRec::instance().currentOperation() == OPER_SHUTDOWNING ||
        !ui->centralWidget->isEnabled())
        return;

    QString coBoxRecStr = ui->coBoxRecorder->currentText();
    if (!coBoxRecStr.size())
        return;

    int fstDevIdx = coBoxRecStr.indexOf("(dev='") + QString("(dev='").size();
    int sndDevIdx = coBoxRecStr.indexOf("')");
    QString devStr = coBoxRecStr.mid(fstDevIdx, sndDevIdx - fstDevIdx);

//#
//    QFile file(devStr);
//    bool result = file.open(QIODevice::ReadOnly);
//    if (result != mIsDevFileSuccessOpened)
//    {
//        mIsDevFileSuccessOpened = result;
//        //qDebug () << "***DISC FILE STATUS CHANGED***";
//        slotRequestDriveDiscInfo();
//    }

    int fd;
    if(( fd = ::open(devStr.toStdString().c_str(), O_RDONLY|O_NONBLOCK)) != -1)
    {
        int result=ioctl(fd, CDROM_DRIVE_STATUS, CDSL_NONE);

        ::close(fd);

        QString diskInfo;

        switch(result)
        {
            // No disk info
            case CDS_NO_INFO:
                mDiscIsChanged = true;
                mDiscInfo.invalid();
            break;
            //No disk
            case CDS_NO_DISC:
                mDiscIsChanged = true;
                mDiscInfo.invalid();
            break;

            //Tray open
            case CDS_TRAY_OPEN:
                mDiscIsChanged = true;
                mDiscInfo.invalid();
            break;

            //Drive not ready
            case CDS_DRIVE_NOT_READY:
                mDiscIsChanged = true;
            //Drive ok
            case CDS_DISC_OK:
            {
              if(mIsDevFileSuccessOpened)
                    return;

              mIsDevFileSuccessOpened = true;
              slotRequestDiscInfo();
              return;

            }break;
        }

        ui->teRecDiscInfo->clear();
        updateDriveInfo();
        updateDiscInfo();
        mIsDevFileSuccessOpened = false;
        ui->btnRecord->setEnabled(false);
    }
}


DiscRecWorkingThread::DiscRecWorkingThread(QObject* parent)
    :QThread(parent)
{
}

void DiscRecWorkingThread::run()
{
    while (true)
    {
        mOperationTypeQueueLock.lock();
        if (mOperationTypeQueue.isEmpty())
        {
            mOperationTypeQueueLock.unlock();
            usleep(250 * 1000);
            continue;
        }

        //! Текущая выполняемая операция
        OperationType operationType = mOperationTypeQueue.dequeue();
        mOperationTypeQueueLock.unlock();

        //! Имя привода
        mDriveNameQueueLock.lock();
        QString driveName;
        if (!mDriveNameQueue.isEmpty())
            driveName = mDriveNameQueue.dequeue();
        mDriveNameQueueLock.unlock();

        //! Имя диска
        mDiscTitleQueueLock.lock();
        QString discTitle;
        if (!mDiscTitleQueue.isEmpty())
            discTitle = mDiscTitleQueue.dequeue();
        mDiscTitleQueueLock.unlock();

        //! Путь к каталогу с файлами на запись
        mDirPathQueueLock.lock();
        QString dirPath;
        if (!mDirPathQueue.isEmpty())
            dirPath = mDirPathQueue.dequeue();
        mDirPathQueueLock.unlock();

        //! Путь к создаваемому/записываемому файлу ISO-образа
        mIsoPathQueueLock.lock();
        QString isoPath;
        if (!mIsoPathQueue.isEmpty())
            isoPath = mIsoPathQueue.dequeue();
        mIsoPathQueueLock.unlock();

        mExcludeFilePathQueueLock.lock();
        QString excludeFilePath;
        if (!mExcludeFilePathQueue.isEmpty())
            excludeFilePath = mExcludeFilePathQueue.dequeue();
        mExcludeFilePathQueueLock.unlock();

        mFirstWriteQueueLock.lock();
        bool isFirstWrite = false;
        if (!mFirstWriteQueue.isEmpty())
            isFirstWrite = mFirstWriteQueue.dequeue();
        mFirstWriteQueueLock.unlock();

        //! Быстрое или полное стирание
        mIsFastBlankQueueLock.lock();
        bool isFastBlank;
        if (!mIsFastBlankQueue.isEmpty())
            isFastBlank = mIsFastBlankQueue.dequeue();
        mIsFastBlankQueueLock.unlock();

        switch (operationType)
        {
        case OPER_CREATION_ISO:
            DiscRec::instance().makeISO(discTitle, dirPath, isoPath, excludeFilePath, isFirstWrite);
            break;
        case OPER_RECORDING_DISC:
            DiscRec::instance().recordISO(driveName.toAscii().data(), isoPath.toAscii().data());
            break;
        case OPER_ERASING_DISC:
            DiscRec::instance().eraseDisc(driveName.toAscii().data(), isFastBlank);
            break;
        case OPER_FORMATTING_DISC:
            DiscRec::instance().formatDisc(driveName.toAscii().data());
            break;
        case OPER_SCANNING_RECORDERS:
            DiscRec::instance().scanDrives();
            break;
        case OPER_INITIALIZING:
            DiscRec::instance().initialize();
            break;
        case OPER_SHUTDOWNING:
            exit(DiscRec::instance().shutdown());
            return;
        case OPER_CANCELLING:
            DiscRec::instance().cancelAndWait();
            break;
        case OPER_NO:
            break;
        case OPER_DRIVE_INFO:
        {
            if (!driveName.size())
                break;
            DriveInfoConst driveInfo = DiscRec::instance().driveInfoConst(driveName);
            emit sigReceivedDriveInfo(driveInfo);
            break;
        }
        case OPER_DISC_INFO:
        {
            if (!driveName.size())
                break;
            DiscInfo discInfo = DiscRec::instance().discInfo(driveName); // тормозящий вызов
            //if (discInfo.isValid())
                emit sigReceivedDiscInfo(discInfo);
            break;
        }
        }
    }
}

void DiscRecWorkingThread::initialize()
{
    QMutexLocker operationTypeQueueLocker(&mOperationTypeQueueLock);
    mOperationTypeQueue.enqueue(OPER_INITIALIZING);

    if (isRunning())
        start();
}

void DiscRecWorkingThread::shutdown()
{
    QMutexLocker operationTypeQueueLocker(&mOperationTypeQueueLock);
    mOperationTypeQueue.clear();
    mOperationTypeQueue.enqueue(OPER_SHUTDOWNING);

    if (isRunning())
        start();
}

void DiscRecWorkingThread::makeISO(QString discTitle, QString dirPath, QString isoPath, QString excludeFilePath, bool firstWrite)
{        
    QMutexLocker discTitleQueueLocker(&mDiscTitleQueueLock);
    mDiscTitleQueue.enqueue(discTitle);

    QMutexLocker dirPathQueueLocker(&mDirPathQueueLock);
    mDirPathQueue.enqueue(dirPath);

    QMutexLocker isoPathQueueLocker(&mIsoPathQueueLock);
    mIsoPathQueue.enqueue(isoPath);

    QMutexLocker excludeFilePathQueueLocker(&mExcludeFilePathQueueLock);
    mExcludeFilePathQueue.enqueue(excludeFilePath);

    QMutexLocker operationTypeQueueLocker(&mOperationTypeQueueLock);
    mOperationTypeQueue.enqueue(OPER_CREATION_ISO);

    QMutexLocker firstWriteQueueLocker(&mFirstWriteQueueLock);
    mFirstWriteQueue.enqueue(firstWrite);

    if (isRunning())
        start();
}

void DiscRecWorkingThread::recordISO(QString driveName, QString isoPath)
{
    QMutexLocker driveNameQueueLocker(&mDriveNameQueueLock);
    mDriveNameQueue.enqueue(driveName);

    QMutexLocker isoPathQueueLocker(&mIsoPathQueueLock);
    mIsoPathQueue.enqueue(isoPath);

    QMutexLocker operationTypeQueueLocker(&mOperationTypeQueueLock);
    mOperationTypeQueue.enqueue(OPER_RECORDING_DISC);

    if (isRunning())
        start();
}

void DiscRecWorkingThread::eraseDisc(QString driveName, bool isFastBlank)
{
    QMutexLocker driveNameQueueLocker(&mDriveNameQueueLock);
    mDriveNameQueue.enqueue(driveName);

    QMutexLocker isFastBlankQueueLocker(&mIsFastBlankQueueLock);
    mIsFastBlankQueue.enqueue(isFastBlank);

    QMutexLocker operationTypeQueueLocker(&mOperationTypeQueueLock);
    mOperationTypeQueue.enqueue(OPER_ERASING_DISC);

    if (isRunning())
        start();
}

void DiscRecWorkingThread::formatDisc(QString driveName)
{
    QMutexLocker driveNameQueueLocker(&mDriveNameQueueLock);
    mDriveNameQueue.enqueue(driveName);

    QMutexLocker operationTypeQueueLocker(&mOperationTypeQueueLock);
    mOperationTypeQueue.enqueue(OPER_FORMATTING_DISC);

    if (isRunning())
        start();
}

void DiscRecWorkingThread::scanDrives()
{
    QMutexLocker operationTypeQueueLocker(&mOperationTypeQueueLock);
    mOperationTypeQueue.enqueue(OPER_SCANNING_RECORDERS);

    if (isRunning())
        start();
}

void DiscRecWorkingThread::getDriveInfo(QString driveName)
{
    QMutexLocker driveNameQueueLocker(&mDriveNameQueueLock);
    mDriveNameQueue.enqueue(driveName);

    QMutexLocker operationTypeQueueLocker(&mOperationTypeQueueLock);
    mOperationTypeQueue.enqueue(OPER_DRIVE_INFO);

    if (isRunning())
        start();
}

void DiscRecWorkingThread::getDiscInfo(QString driveName)
{
    QMutexLocker driveNameQueueLocker(&mDriveNameQueueLock);
    mDriveNameQueue.enqueue(driveName);

    QMutexLocker operationTypeQueueLocker(&mOperationTypeQueueLock);
    mOperationTypeQueue.enqueue(OPER_DISC_INFO);

    if (isRunning())
        start();
}
