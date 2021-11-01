#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "discrec.h"

#include <QMainWindow>
#include <QThread>
#include <QProcess>
#include <QQueue>
#include <QMutex>
#include <QtGlobal>

#if QT_VERSION >= 0x050000
#define toAscii toLatin1
#endif

namespace Ui {
class MainWindow;
}

class QProgressBar;
class DiscRecWorkingThread;

struct MainFileInfo
{
    QString fileName;
    qint64  size;
    int     parts;
    int     partsWritten;
    int     partsForWrite;
};

/*!
    \class MainWindow
    \brief Класс формы c главным окном
*/
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    //! Конструктор
    /*!
     * \brief конструктор главной формы
     * \param parent родитель
     */
    explicit MainWindow(QWidget* parent = 0);

    //! Деструктор
    /*!
     * \brief деструктор главной формы
     * Останавливает главный рабочий поток и таймер для получения информации о диске/приводе
     */
    ~MainWindow();

protected:
    //! Закрытие формы
    /*!
     * \brief обработчик события закрытия главной формы - выполнение проверок текущей работы
     * \param event событие закрытия формы
     */
    void closeEvent(QCloseEvent* event);

private:
    Ui::MainWindow* ui;                             //!< UI с граф. интерфейсом
    QProgressBar* mProgBar;                         //!< прогресс-бар выполнения операции
    DiscRecWorkingThread* mDiscRecWorkingThread;    //!< поток для выполнения в нем длинных операций (рабочий поток)
    DriveInfoConst mDriveInfo;                      //!< последняя полученная информация о приводе
    QReadWriteLock mDriveInfoLock;                  //!< соответсвующий последней полученной информации о приводе объект блокировки
    DiscInfo mDiscInfo;                             //!< последняя полученная информация о диске
    QReadWriteLock mDiscInfoLock;                   //!< соответсвующий последней полученной информации о диске объект блокировки
    OperationType mLastSuccessOperation;            //!< последняя успешно выполненная операция
    bool mLastOperationResult;                      //!< последний результат. дадада спагетти код требует
    QTimer* mTimerUpdateDiscInfo;                   //!< таймер для обновления информации о диске
    bool mIsDevFileSuccessOpened;                   //!< успешно ли открыт файл /dev/... - используется для переодической проверки статусапривода/диска
    QString mDriveInfoStr;                          //!< строка с информацией о приводе
    int mDiscInfoTryCounts;                         //!< количество попыток получения информации о диске
    bool mDiscIsChanged;                            //!< признак смены диска
    QString mDiscTypeName;                          //!< строка с типом диска
    burn_disc_status mDiscStatus;                   //!< статус диска
    int mDiscType;                                  //!< тип диска
    int mFormatStatus;                              //!< статус форматирования диска
    bool mIsDiscErasable;                           //!< признак того, что диск стираемый
    quint64 mAvailableDiscSpace;                    //!< свободное место на диске
    QString mBaseDirName;                           //!< путь к директории с файлами восстановления
    quint64 mTotalFilesSize;                        //!< общий объем файлов, необходимых для записи
    quint64 mWrittenSize;                           //!< текущий объем записанных файлов
    quint64 mSizeForWrite;                          //!< объем файлов, подготовленных для записи текущего диска
    quint64 mMainFilesSize;                         //!< объем файлов восстановления
    QList<QString> mWrittenFilesList;               //!< список файлов для записи
    int mDiscNumber;                                //!< номер текущего диска
    QProcess *mSplitProcess;                        //!< указатель на процесс split
    int mSplitRetCode;                              //!< код возврата процесса split
    bool mSplitWorking;                             //!< признак работы процесса split
    QList<struct MainFileInfo> mMainFilesInfo;      //!< список файлов восстановления
    bool mFirstWrite;                               //!< признак необходимости создания загрузочного диска

    //! Получение списка файлов в директории
    /*!
     * \brief Получить список всех файлов и их размер в заданной директории
     * \param dirName название директории
     * \param fileNames объект для размещения списка файлов
     * \return общий размер файлов
     */
    quint64 getFilesSizeInDir(QString dirName, QList<QString> &fileNames);

    //! Обновление мета файла
    /*!
     * \brief Обновляет метафайл (добавляет в него размер файлов с архивами)
     * \return результат обновления (успешно или нет)
     */
    bool updateMetaFile();

    //! Удаление split-файлов
    /*!
     * \brief Удаляет созданные при прошлых запусках split-файлы
     */
    void deleteSplitFiles();

    //! Создание файла, со списком файлов, не подлежащих записи на диск
    /*!
     * \brief Создает файл, со списком файлов, не подлежащих записи на диск
     * \param excludeFileName путь к файлу
     * \return результат создания файла (успешно или нет)
     */
    bool createExcludeFileName(QString excludeFileName);

    //! Запуск программы split
    /*!
     * \brief Запуск программы split для выполнения оперции, заданной аргументами
     * \param args список аргументов программы
     * \return true если запуск успешный, false в противном случае
     */
    bool runSplit(QStringList args);

    //! Останов программы split
    /*!
     * \brief Выполнение завершения программы split
     * \param msecWaitTime таймаут для ожидания завершения программы
     */
    void stopSplit(int msecWaitTime = 3000);

    //! Отображение информации об объеме записанных файлов
    /*!
     * \brief Выводит в заголовок программы информацию об объеме записанных файлов и их общем объёме
     */
    void showRecordedInfo();

    //! Обновляет список файлов, подлежащих записи
    /*!
     * \brief Обновляет список файлов, подлежащих записи после каждой операции записи диска
     */
    void updateFilesList(bool result);

    //! Выполняет проверку возможности записи данных на диск
    /*!
     * \brief Выполняет проверку возможности записи данных на диск
     */
    void checkDisc();

    //! Попытаться завершить поток, в случае его зависания терминейтить его принудительно, после всего этого очистить память из под объекта потока и занулить указатель
    /*!
     * \brief Завершить поток
     * \param thread поток
     * \param msecWaitTime время в миллисекундах для ожидания завершения потока
     */
    void finishAndDeleteThread(QThread* thread, int msecWaitTime = Const::msecWaitThreadTime);

    //! Попытаться завершить процесс, в случае его зависания килять его принудительно, после всего этого очистить память из под объекта процесса и занулить указатель
    /*!
     * \brief Завершить процесс
     * \param process процесс
     * \param msecWaitTime время в миллисекундах для ожидания завершения процесса
     */
    void finishAndDeleteProcess(QProcess* process, int msecWaitTime = Const::msecWaitThreadTime);

    //! Заблокировать/разблокировать интерфейс пользователя при начале/завершении записи
    /*!
     * \brief Заблокировать/разблокировать интерфейс пользователя при начале/завершении записи
     * \param state состояние блокировки: true - разблокировать, false - заблокировать
     */
    void setGUIState(bool state);

    //! Блокировка интерфейса при начале записи
    /*!
     * \brief Блокировка интерфейса при начале записи
     */
    void lockGUIRecordStart();

    //! Разблокировка интерфейса при окончании записи
    /*!
     * \brief Разблокировка интерфейса при окончании записи
     */
    void unlockGUIRecordFinish();

    //! Выполнить запись
    /*!
     * \brief Выполнить запись диска (диск должен быть подходящим и подготовленным к записи)
     * \param availableDiscSpace достпное свободное место на диске
     */
    void makeRecord(off_t availableDiscSpace);

private slots:

    //! Сигнал об успешном запуске программы split
    /*!
     * \brief сигнал об успешном запуске программы split
     */
    void slotSplitStarted();

    //! Сигнал о завершении программы split
    /*!
     * \brief сигнал о завершении программы split
     * \param result код возврата программы
     */
    void slotSplitFinished(int code);

    //! Слот для вывода на форму пришедшей информации о приводе и обновления внутренних членов с этой информацией
    /*!
     * \brief Слот проверки пришедшей информации
     * \param driveInfo информация о приводе     
     */
    void slotReceiveDriveInfo(DriveInfoConst driveInfo);

    //! Слот для вывода на форму пришедшей информации о диске и обновления внутренних членов с этой информацией
    /*!
     * \brief Слот проверки пришедшей информации
     * \param discInfo информация о диске
     */
    void slotReceiveDiscInfo(DiscInfo discInfo);

    //! Проверка возможности записи (запрос информации об устройстве)
    /*!
     * \brief Проверка возможности записи (запрос информации об устройстве)
     */
    void slotRequestDriveInfo();

    //! Проверка возможности записи (запрос информации о диске)
    /*!
     * \brief Проверка возможности записи (запрос информации о диске)
     */
    void slotRequestDiscInfo();

    //! Поиск приводов
    /*!
     * \brief Поиск приводов
     */
    void slotScanRecorders();

    //! Смена текущего привода
    /*!
     * \brief Смена текущего привода
     * \param index индекс текущего выбранного привода в комбобоксе
     */
    void slotRecorderChanged(int index);

    //! Выбор пути к файлам на запись
    /*!
     * \brief Выбор пути к файлам на запись
     */
    void slotSelectPath();

    //! Изменение пути к файлам на запись
    /*!
     * \brief Изменение пути к файлам на запись
     * \param text путь к файлам на запись
     */
    void slotFilesPathTextChanged(const QString& text);

    //! Запись
    /*!
     * \brief Нажатие на кнопку "Запись"
     */
    void slotRecord();

    //! Отмена
    /*!
     * \brief Нажатие на кнопку "Отмена"
     */
    void slotCancel();

    //! Получение сообщений от рабочего потока
    /*!
     * \brief Получение сообщений от рабочего потока
     * \param mstType тип сообщения
     * \param text текст сообщения
     */
    void slotReceiveMessage(MessageType mstType, QString text);

    //! Завершение выполнения операции
    /*!
     * \brief Завершение выполнения операции
     * \param finishedOperation выполненная операция
     * \param result результат выполнения операции
     */
    void slotOperationFinished(OperationType finishedOperation, bool result);

    //! Обновление информации о приводе
    /*!
     * \brief Обновление информации о приводе
     * \param overwrite перетереть ли инфу в текст боксе (в противном случае присоединить)
     * \return в случае успеха true, в случае ошибки false
     */
    bool updateDriveInfo(bool overwrite = true);

    //! Обновление информации о диске
    /*!
     * \brief Обновление информации о диске
     * \param overwrite перетереть ли инфу в текст боксе (в противном случае присоединить)
     * \return в случае успеха true, в случае ошибки false
     */
    bool updateDiscInfo(bool overwrite = false);

    //! Обновление информации о диске и приводе (внешняя проверка /dev/...)
    /*!
     * \brief Обновление информации о диске и приводе (внешняя проверка /dev/...)
     */
    void updateDiscDriveInfoFileCheck();
};

/*!
    \class MainWindow
    \brief Поточный класс для выполнения основных блокирующих операций
*/
class DiscRecWorkingThread : public QThread
{
    Q_OBJECT

public:
    //! Конструктор
    /*!
     * \brief Конструктор
     * \param parent родитель
     */
    DiscRecWorkingThread(QObject* parent = 0);

    //! Основной рабочий метод
    /*!
     * \brief Рабочий метод, в котором выполняются делегация DiscRec
     */
    void run();

    //! Инициализация библиотек
    /*!
     * \brief Инициализация библиотек
     */
    void initialize();

    //! Завершение работы с библиотеками
    /*!
     * \brief Завершение работы с библиотеками
     */
    void shutdown();

    //! Создание ISO-образа для записи
    /*!
      \brief Предварительно подготавливает ISO-образ для записи на диск
      \param discTitle метка диска
      \param dirPath полный путь к каталогу с файлами на запись
      \param isoPath путь к создаваемому файлу ISO-образа
    */
    void makeISO(QString discTitle, QString dirPath, QString isoPath, QString excludeFilePath, bool firstWrite);

    //! Запись образа на компакт-диск
    /*!
     * \brief Запись сформированного ISO-образа выбранным приводом
     * \param driveName имя привода
     * \param isoPath путь к записываемому образу
     */
    void recordISO(QString driveName, QString isoPath);

    //! Стереть диск
    /*!
     * \brief Стирание диска
     * \param driveName имя привода
     * \param isFastBlank быстрое ли стирание только заголовка диска (true) или полное всего диска (false). Для DVD-RW быстрое стирание подходит только для DAO
     */
    void eraseDisc(QString driveName, bool isFastBlank = true);

    //! Форматирование диска
    /*!
     * \brief Форматирование диска
     * \param driveName имя привода
     */
    void formatDisc(QString driveName);

    //! Выполнить обнаружение приводов
    /*!
     * \brief Повторное сканирование и обновление членов mDrives и mDrivesNamesMap
     */
    void scanDrives();

    //! Получить информацию о приводе
    /*!
     * \brief получить информацию о приводе
     * \param driveName имя привода
     */
    void getDriveInfo(QString driveName);

    //! Получить информацию о диске
    /*!
     * \brief получить информацию о диске
     * \param driveName имя привода
     */
    void getDiscInfo(QString driveName);

signals:
    //! Сигнал о получении информации о приводе. Отсылается если информация актуальная
    /*!
     * \brief сигнал о получении информации о приводе. Отсылается если информация актуальная
     * \param driveName имя привода, для которого получена информация о нем
     * \param driveInfo информация о приводе
     * \param discInfo информация о диске
     */
    void sigReceivedDriveInfo(DriveInfoConst driveInfo);

    //! Сигнал о получении информации о диске. Отсылается если информация актуальная
    /*!
     * \brief сигнал о получении информации о диске. Отсылается если информация актуальная
     * \param driveName имя привода, для которого получена информация о диске в нем
     * \param discInfo информация о диске
     */
    void sigReceivedDiscInfo(DiscInfo discInfo);

private:
    QQueue<OperationType> mOperationTypeQueue;          //!< очередь выполняемых операций
    QMutex mOperationTypeQueueLock;                     //!< соответствующий мьютекс для очереди выполняемых операций
    QQueue<QString> mDriveNameQueue;                    //!< очередь имен приводов из комбобокса
    QMutex mDriveNameQueueLock;                         //!< соответствующий мьютекс для очереди имен приводов из комбобокса
    QQueue<QString> mDiscTitleQueue;                    //!< очередь имен диска
    QMutex mDiscTitleQueueLock;                         //!< соответствующий мьютекс для очереди имен диска
    QQueue<QString> mDirPathQueue;                      //!< очередь полных путей к каталогу с файлами на запись
    QMutex mDirPathQueueLock;                           //!< соответствующий мьютекс для очереди полных путей к каталогу с файлами на запись
    QQueue<QString> mIsoPathQueue;                      //!< очередь путей к создаваемому/записываемому файлу ISO-образа
    QMutex mIsoPathQueueLock;                           //!< соответствующий мьютекс для очереди путей к создаваемому/записываемому файлу ISO-образа
    QQueue<bool> mIsFastBlankQueue;                     //!< очередь параметров на быстрое(true)/полное(false) стирание диска
    QMutex mIsFastBlankQueueLock;                       //!< соответствующий мьютекс для очереди параметров на быстрое(true)/полное(false) стирание диска
    QQueue<QString> mExcludeFilePathQueue;              //!< очередь путей к файлу, содержащему список исключаемых из ISO-образа файлов
    QMutex mExcludeFilePathQueueLock;                   //!< соответствующий мьютекс для очереди путей к файлу, содержащему список исключаемых из ISO-образа файлов
    QQueue<bool> mFirstWriteQueue;                      //!< очередь параметров на создание загрузочного (true) или нет (false) диска
    QMutex mFirstWriteQueueLock;                        //!< соответствующий мьютекс для очереди параметров на создание загрузочного (true) или нет (false) диска

};

#endif // MAINWINDOW_H

