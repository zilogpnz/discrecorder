#ifndef DISCREC_H
#define DISCREC_H

#include "common.h"

/*! Следующее макроопределение (HAVE_STDINT_H) нужно при подключении заголовчника libisofs, директива QMAKE_CFLAGS в pro файле почему то не работает */
#define HAVE_STDINT_H
#define HAVE_TIMEGM // на всякий случай если поменяется *.pro/pri файл

extern "C"
{
//#include "libburn.h"
//#include "libisofs.h"
}

#include <QObject>
#include <QMap>
#include <QList>
#include <QReadWriteLock>
#include <QString>
#include <QProcess>

//! Тип сообщения
enum MessageType
{
    MSG_INFORMATION,                    //!< информационное
    MSG_WARNING,                        //!< предупреждающее
    MSG_CRITICAL,                       //!< критическая ошибка
    MSG_EXECUTION_PERCENT,              //!< процент выполнения операции
    MSG_CURRENT_OPERATION               //!< выполняющаяся операция
};

enum EXIT_CODES
{
    EXIT_NORMAL,
    EXIT_CANCEL,
    EXIT_ERROR
};

//! Возможные текущие выполняемые операции 7 1 9 9
enum OperationType
{
    OPER_NO,                            //!< ничего не выполняется
    OPER_SCANNING_RECORDERS,            //!< поиск приводов
    OPER_CREATION_ISO,                  //!< создание образа
    OPER_RECORDING_DISC,                //!< запись диска
    OPER_ERASING_DISC,                  //!< стирание диска
    OPER_FORMATTING_DISC,               //!< форматирование диска
    OPER_CANCELLING,                    //!< отмена операции
    OPER_INITIALIZING,                  //!< инициализация библиотек
    OPER_SHUTDOWNING,                   //!< завершение работы с библиотеками
    OPER_DRIVE_INFO,                    //!< запрос/получение информации о приводе
    OPER_DISC_INFO                      //!< запрос/получение информации о диске
};

enum burn_drive_status
{
    BURN_DRIVE_IDLE,              //!< никаких операций не производится
    BURN_DRIVE_SPAWNING,          //!< подготовка к выполнению операции, т.н. раскрутка диска (чтение/запись и т.п., но еще не начавшиеся)
    BURN_DRIVE_READING,           //!< чтение данных с диска
    BURN_DRIVE_WRITING,           //!< запись данных на диск
    BURN_DRIVE_WRITING_LEADIN,    //!< запись Lead-In
    BURN_DRIVE_WRITING_LEADOUT,   //!< запись Lead-Out
    BURN_DRIVE_ERASING,           //!< стирание диска
    BURN_DRIVE_GRABBING,          //!< захват привода
    BURN_DRIVE_WRITING_PREGAP,    //!< запись нулей перед треком с полезными данными
    BURN_DRIVE_CLOSING_TRACK,     //!< закрытие трека (только для TAO)
    BURN_DRIVE_CLOSING_SESSION,   //!< закрытие сессии (только для TAO)
    BURN_DRIVE_FORMATTING,        //!< форматирование диска
    BURN_DRIVE_READING_SYNC,      //!< устройство занято в синхронном чтении (если вы это видите то оно было прервано)
    BURN_DRIVE_WRITING_SYNC       //!< устройство занято в синхронной записи (если вы это видите то оно было прервано)
};

enum burn_disc_status
{
    BURN_DISC_UNREADY,      //!< текущий статус неизвестен
    BURN_DISC_BLANK,        //!< чистый диск. Готово к записи
    BURN_DISC_EMPTY,        //!< диска нет
    BURN_DISC_APPENDABLE,   //!< незакрытый диск. Готово к добавлению другой сессии
    BURN_DISC_FULL,         //!< диск с данными, финализированный. Готов только для чтения
    BURN_DISC_UNGRABBED,    //!< устройство не захвачено в момент получения статуса диска
    BURN_DISC_UNSUITABLE    //!< диск не подходит для записи
};

enum burn_disc_format
{
    BURN_FORMAT_IS_FORMATTED,
    BURN_FORMAT_IS_UNFORMATTED,
    BURN_FORMAT_IS_UNKNOWN
};

//! Предварительное объявление структуры с информацией о диске
struct DiscInfo;

//! Предварительное объявление структуры с информацией о приводе
struct DriveInfoConst;

struct burn_drive
{
    QString driveDevName;
};

struct burn_drive_info
{
    struct burn_drive *drive;

    QString vendor;
    QString product;
    QString revision;
    QString location;

    bool dvdRAM;
    bool dvdRMinus;
    bool dvdRPlus;
    bool dvdRWMinus;
    bool dvdRWPlus;
    bool cdR;
    bool cdRW;
    bool bdR;
};


/*!
    \class DiscRec
    \brief Главный интерфейсный класс-синглтон для работы с ISO-образами и записью дисков

    Для получения объекта юзать метод instance(), интерфейс которого унаследован от BaseSingletone
*/
class DiscRec : public QObject, public BaseSingletone<DiscRec>
{
Q_OBJECT
    //! Класс с информацией о диске дружественный
    friend struct DiscInfo;

    //! Класс с информацией о устройстве дружественный
    friend struct DriveInfoConst;

public:

    //! Инициализация
    /*!
     * \brief Инициализация, требуемая для дальнейшей работы с классом
     * \return результат инициализации (true - успех, false - неудача)
     */
    bool initialize();

    //! Завершение работы
    /*!
     * \brief Завершение работы, когда функции класса больше не требуется. Если идет какая-либо операция, то она прервана не будет
     * \return результат завершения работы (true - успех, false - неудача, если выполняется какая-либо операция)
     */
    bool shutdown();

    //! Прервать процесс формирования образа или записи/стирания диска (неблокирующая функция)
    /*!
     * \brief Дает команду прерывать одну из долгих операций и сразу же возвращает управление
     */
    void cancel();

    //! Прервать процесс формирования образа или записи/стирания диска (блокирующая функция)
    /*!
     * \brief Пока одна из долгих операций не прервется, управление не возвращается
     */
    void cancelAndWait();

    //! Получить текущую выполняемую операцию
    /*!
     * \brief Возвращает текущую выполняемую операцию. Потокобезопасный метод
     * \return текущая выполняемая операция
     */
    OperationType currentOperation();

    //! Создание ISO-образа для записи
    /*!
      \brief Предварительно подготавливает ISO-образ для записи на диск
      \param discTitle метка диска
      \param dirPath полный путь к каталогу с файлами на запись
      \param isoPath путь к создаваемому файлу ISO-образа
      \param excludeFilePath путь к файлу со списком, не включаемых в образ файлов
      \param isFirstWrite флаг необходимости создания загрузочного диска
      \return В случае успешного создания файлового образа: true. В случае ошибки: false.
    */
    bool makeISO(QString discTitle, QString dirPath, QString isoPath, QString excludeFilePath, bool isFirstWrite);

    //! Запись образа на компакт-диск
    /*!
     * \brief Запись сформированного ISO-образа выбранным приводом
     * \param driveName имя привода из карты mDrivesNamesMap
     * \param isoPath путь к записываемому образу
     * \return В случае успешной записи диска: true. В случае ошибки: false.
     */
    bool recordISO(QString driveName, const char* isoPath);

    //! Стереть диск
    /*!
     * \brief Стирание диска
     * \param driveName имя привода из карты mDrivesNamesMap
     * \param isFastBlank быстрое ли стирание только заголовка диска (true) или полное всего диска (false). Для DVD-RW быстрое стирание подходит только для DAO
     * \return В случае успешного стирания диска: true. В случае ошибки: false.
     */
    bool eraseDisc(QString driveName, bool isFastBlank = true);

    //! Форматирование диска
    /*!
     * \brief Форматирование диска
     * \param driveName имя привода из карты mDrivesNamesMap
     * \return В случае успешного форматирования диска: true. В случае ошибки: false.
     *
     * Форматирует DVD-RW диск в формат с ограниченной перезаписью (из формата с последовательной записью) или новый BD-диск.
     * О форматах: http://www.freebsd.org/doc/ru_RU.KOI8-R/books/handbook/creating-dvds.html
     */
    bool formatDisc(QString driveName);

    //! Выполнить обнаружение приводов
    /*!
     * \brief Повторное сканирование и обновление членов mDrives и mDrivesNamesMap
     * \return В случае успешного выполнения поиска: true. В случае ошибки: false.
     */
    bool scanDrives();

    //! Получить карту обнаруженных приводов
    /*!
     * \brief Возвращает карту приводов
     * \return карта обнаруженных приводов, ключ - порядковый номер привода, значение - имя
     */
    QList<QString> drivesList() const;

    //! Получить информацию о диске (актуальная на момент вызова)
    /*!
     * \brief Получить информацию о диске
     * \param driveName имя привода из карты mDrivesNamesMap
     * \return информация о диске
     *
     * После получения экземпляра структуры с информацией сразу же вызвать её метод isValid() для проверка валидности информации
     */
    DiscInfo discInfo(QString driveName);

    //! Получить информацию о приводе (постоянная)
    /*!
     * \brief Получить информацию о приводе
     * \param driveName имя привода из карты mDrivesNamesMap
     * \return информация о приводе
     */
    DriveInfoConst driveInfoConst(QString driveName);

signals:
    //! Сигнал с сообщением
    /*!
     * \brief Генерируется для передачи сообщения всем, кто подцепился на этот сигнал
     * \param msgType тип сообщения
     * \param text текст отображаемого сообщения
     */
    void message(MessageType msgType, QString text);

    //! Завершение операции
    /*!
     * \brief Генерируется при завершении операции создания образа, записи/стирания/форматирования диска, поиска приводов
     * \param result результат выполнения операции
     */
    void finished(OperationType currOper, bool result);

private slots:
    //! Сигнал об успешном запуске программы xorriso
    /*!
     * \brief сигнал об успешном запуске программы xorriso
     */
    void slotXorrisoStarted();

    //! Сигнал о завершении программы xorriso
    /*!
     * \brief сигнал о завершении программы xorriso
     * \param result код возврата программы
     */
    void slotXorrisoFinished(int code);

    //! Сигнал о наличии данных в потоке stderr программы xorriso
    /*!
     * \brief сигнал о наличии данных в потоке stderr программы xorriso
     */
    void slotXorrisoReadStderr();

    //! Сигнал о наличии данных в потоке stdout программы xorriso
    /*!
     * \brief сигнал о наличии данных в потоке stdout программы xorriso
     */
    void slotXorrisoReadStdout();

private:
    DiscRec();
    DiscRec(const DiscRec&);
    virtual ~DiscRec();
    DiscRec& operator=(const DiscRec&);
    friend class BaseSingletone<DiscRec>;

    QProcess *mXorrisoProcess;                          //!< указатель на процесс xorriso
    int mXorrisoRetCode;                                //!< код возврата процесса xorriso
    bool mXorrisoWorking;                               //!< признак работы процесса xorriso
    int mDrivesCount;                                   //!< количество приводов
    bool mScanDrives;                                   //!< признак выполнения операция поиска приводов
    int mCurDrive;                                      //!< индекс текущего привода
    QMap <int, QString> mProfilesMap;                   //!< список возможных профилей дисков
    QMap <burn_disc_status, QString> mDiscStatusMap;    //!< список возможных статусов диска
    QString mDiscProfileStr;                            //!< строка с профилем текущего диска
    burn_disc_status mDiscStatus;                       //!< статус диска
    burn_disc_format mDiscFormat;                       //!< статус форматирования диска
    off_t mDiscFormatSize;                              //!< размер форматирования диска
    int mDiscFormatNum;                                 //!< количество форматирований диска
    int mPercent;                                       //!< процент выполнения текущей операции
    double mDiskFreeSize;                               //!< свободное место на диске
    OperationType mCurrOper;                            //!< текущая операция
    QReadWriteLock mCurrOperLock;                       //!< соответсвующий текущей операции объект блокировки
    QList<struct burn_drive_info*> mDrives;             //!< указатель на структуры с информацией о приводах
    QMap<int, QString> mDrivesNamesMap;                 //!< карта соответствия порядковых номеров приводов и их названий
    QList<struct burn_drive*> mDrivesGrabbedList;       //!< список захваченных устройств
    QReadWriteLock mDrivesGrabbedListLock;              //!< соответсвующий списку захваченных устройств объект блокировки

    //! Запуск программы xorriso
    /*!
     * \brief Запуск программы xorriso для выполнения оперции, заданной аргументами
     * \param args список аргументов программы
     * \return true если запуск успешный, false в противном случае
     */
    bool runXorriso(QStringList args);

    //! Останов программы xorriso
    /*!
     * \brief Выполнение завершения программы xorriso
     * \param msecWaitTime таймаут для ожидания завершения программы
     */
    void stopXorriso(int msecWaitTime = 3000);

    //! Освобождение памяти, отводившейся для хранения информации о приводах
    /*!
     * \brief Освобождает память, отводившуюмя для хранения информации о приводах
     */
    void clearDrives();    

    //! Установить текущую выполняемую операцию
    /*!
     * \brief Установка текущей выполняемой операции. Потокобезопасный метод
     * \param currOper выполняемая операция
     */
    inline void setCurrentOperation(OperationType currOper);

    //! Получить номер привода из карты mDrivesNamesMap
    /*!
     * \brief Получить номер привода из карты mDrivesNamesMap
     * \param driveName имя привода
     * \return  номер привода если найден, -1 если не найден
     */
    inline int getDriveNumber(QString driveName);

    //! Получить информацию о приводе по его имени из карты mDrivesNamesMap
    /*!
     * \brief Получить указатель на структуру с информацией о приводе burn_drive_info по его имени из карты mDrivesNamesMap
     * \param driveName полное имя устройства из карты mDrivesNamesMap
     * \return указатель на структуру burn_drive_info, либо 0 в случае ошибки
     */
    inline struct burn_drive_info* getDriveInfo(QString driveName);

    //! Получить привод по его имени из карты mDrivesNamesMap
    /*!
     * \brief Получить указатель на структуру привода burn_drive по его имени из карты mDrivesNamesMap
     * \param driveName полное имя устройства из карты mDrivesNamesMap
     * \return указатель на структуру burn_drive, либо 0 в случае ошибки
     */
    inline struct burn_drive* getDrive(QString driveName);

    //! Захватить устройство привода
    /*!
     * \brief Захватить устройство привода для дальнейших манипуляций с ним (запись, стирание)
     * \param drive устройство привода для захвата
     * \return в случае успеха захвата или если уже захвачено true, в случае ошибки захвата false
     */
    bool grabDrive(struct burn_drive* drive);

    //! Захватить устройство привода
    /*!
     * \brief Захватить устройство привода для дальнейших манипуляций с ним (запись, стирание)
     * \param driveName полное имя устройства из карты mDrivesNamesMap
     * \return в случае успеха захвата или если уже захвачено true, в случае ошибки захвата false
     */
    bool grabDrive(QString driveName);

    //! Освободить устройство привода
    /*!
     * \brief Освободить устройство привода после завершения работы с ним
     * \param drive устройство привода для захвата
     * \return в случае успеха свобождения true, в противном случае false
     */
    bool releaseDrive(struct burn_drive* drive);

    //! Освободить устройство привода
    /*!
     * \brief Освободить устройство привода после завершения работы с ним
     * \param driveName полное имя устройства из карты mDrivesNamesMap
     * \return в случае успеха свобождения true, в противном случае false
     */
    bool releaseDrive(QString driveName);

    //! Захвачено ли устройство
    /*!
     * \brief Захвачено ли устройство
     * \param drive устройство привода для захвата
     * \return статус захвата устройства: true в случае если захвачено, в противном случае false
     */
    bool isDriveGrabbed(struct burn_drive* drive);

    //! Захвачено ли устройство
    /*!
     * \brief Захвачено ли устройство
     * \param driveName полное имя устройства из карты mDrivesNamesMap
     * \return статус захвата устройства: true в случае если захвачено, в противном случае false
     */
    bool isDriveGrabbed(QString driveName);

    //! Получить статус диска
    /*!
     * \brief Получить статус диска в выбранном приводе
     * \param drive устройство привода
     * \return статус диска (burn_disc_status) или BURN_DISC_UNREADY в случае ошибки или прерывания выполнения
     *
     * Устройство должно быть захвачено перед вызовом данной функции
     *
     * Перечисление burn_disc_status:
     *
     * BURN_DISC_UNREADY      //!< текущий статус неизвестен
     * BURN_DISC_BLANK        //!< чистый диск. Готово к записи
     * BURN_DISC_EMPTY        //!< диска нет
     * BURN_DISC_APPENDABLE   //!< незакрытый диск. Готово к добавлению другой сессии
     * BURN_DISC_FULL         //!< диск с данными, финализированный. Готов только для чтения
     * BURN_DISC_UNGRABBED    //!< устройство не захвачено в момент получения статуса диска
     * BURN_DISC_UNSUITABLE   //!< диск не подходит для записи
     *
     */

    enum burn_disc_status getDiscStatus(struct burn_drive* drive);

    //! Получить статус диска
    /*!
     * \brief Получить статус диска в выбранном приводе
     * \param driveName полное имя устройства из карты mDrivesNamesMap
     * \return статус диска (burn_disc_status, описанное выше) или BURN_DISC_UNREADY в случае ошибки или прерывания выполнения
     *
     * Устройство должно быть захвачено перед вызовом данной функции
     */
    enum burn_disc_status getDiscStatus(QString driveName);

    //! Получить информацию о форматировании диска
    /*!
     * \brief Функция для получения информации о форматировании диска, была ли она произведена и т.п.
     * \param drive устройство привода
     * \param formatStatus статус форматирования диска
     * \param numFormats количество доступных форматирований
     * \param formatSize размер после форматирования
     * \return в случае успешного получения информации true, в противном случае false
     */
    bool getDiscFormatType(struct burn_drive* drive, int& formatStatus, int& numFormats, off_t& formatSize);

    //! Получить информацию о форматировании диска
    /*!
     * \brief Функция для получения информации о форматировании диска, была ли она произведена и т.п.
     * \param driveName полное имя устройства из карты mDrivesNamesMap
     * \param formatStatus статус форматирования диска
     * \param numFormats количество доступных форматирований
     * \param formatSize размер после форматирования
     * \return в случае успешного получения информации true, в противном случае false
     */
    bool getDiscFormatType(QString driveName, int& formatStatus, int& numFormats, off_t& formatSize);

    //! Получить статус привода
    /*!
     * \brief Получить статус выбранного привода
     * \param drive устройство привода
     * \return статус привода (burn_drive_status)
     *
     * Устройство должно быть захвачено перед вызовом данной функции
     */
    inline enum burn_drive_status getDriveStatus(struct burn_drive* drive);

    //! Получить статус привода
    /*!
     * \brief Получить статус выбранного привода
     * \param driveName полное имя устройства из карты mDrivesNamesMap
     * \param progress структура с информацией о прогрессе, которая изменится в функции (память должна быть выделена)
     * \return статус привода (burn_drive_status)
     *
     * Устройство должно быть захвачено перед вызовом данной функции
     *
     * Перечисление burn_drive_status:
     *
     * BURN_DRIVE_IDLE              //!< никаких операций не производится
     * BURN_DRIVE_SPAWNING          //!< подготовка к выполнению операции, т.н. раскрутка диска (чтение/запись и т.п., но еще не начавшиеся)
     * BURN_DRIVE_READING           //!< чтение данных с диска
     * BURN_DRIVE_WRITING           //!< запись данных на диск
     * BURN_DRIVE_WRITING_LEADIN 	//!< запись Lead-In
     * BURN_DRIVE_WRITING_LEADOUT   //!< запись Lead-Out
     * BURN_DRIVE_ERASING           //!< стирание диска
     * BURN_DRIVE_GRABBING          //!< захват привода
     * BURN_DRIVE_WRITING_PREGAP    //!< запись нулей перед треком с полезными данными
     * BURN_DRIVE_CLOSING_TRACK     //!< закрытие трека (только для TAO)
     * BURN_DRIVE_CLOSING_SESSION   //!< закрытие сессии (только для TAO)
     * BURN_DRIVE_FORMATTING        //!< форматирование диска
     * BURN_DRIVE_READING_SYNC      //!< устройство занято в синхронном чтении (если вы это видите то оно было прервано)
     * BURN_DRIVE_WRITING_SYNC      //!< устройство занято в синхронной записи (если вы это видите то оно было прервано)
     */

    enum burn_drive_status getDriveStatus(QString driveName);

    //! Получить тип диска
    /*!
     * \brief Получить тип диска и описание типа в выбранном приводе
     * \param drive устройство привода
     * \param discType тип диска (значения см. ниже)
     * \param discTypeName строка с типом диска (значения см. ниже)
     * \return в случае успешного получения информации true, в противном случае false
     *
     * Устройство должно быть захвачено перед вызовом данной функции
     *
     * Writes only to profiles 0x09 "CD-R", 0x0a "CD-RW", 0x11 "DVD-R sequential recording", 0x12 "DVD-RAM",
     *      0x13 "DVD-RW restricted overwrite", 0x14 "DVD-RW sequential recording", 0x1a "DVD+RW", 0x1b "DVD+R",
     *      0x2b "DVD+R/DL", 0x41 "BD-R sequential recording", 0x43 "BD-RE", 0xffff "stdio file"
     *      If enabled by burn_allow_untested_profiles() it also writes to profiles 0x15 "DVD-R/DL sequential recording"
     * Read-only are the profiles 0x08 "CD-ROM", 0x10 "DVD-ROM", 0x40 "BD-ROM", For now read-only is BD-R profile (testers wanted) 0x42 "BD-R random recording"
     *
     *
     * 0x09 "CD-R"
     * 0x0a "CD-RW"
     * 0x11 "DVD-R sequential recording"
     * 0x12 "DVD-RAM"
     * 0x13 "DVD-RW restricted overwrite"
     * 0x14 "DVD-RW sequential recording",
     * 0x15 "DVD-R/DL sequential recording",
     * 0x1a "DVD+RW"
     * 0x1b "DVD+R",
     * 0x2b "DVD+R/DL",
     * 0x41 "BD-R sequential recording",
     * 0x43 "BD-RE",
     * 0xffff "stdio file"
     *Note: 0xffff is not a MMC profile but a libburn invention.
     *Read-only are the profiles
     * 0x08 "CD-ROM",
     * 0x10 "DVD-ROM",
     * 0x40 "BD-ROM",
     *Read-only for now is this BD-R profile (testers wanted)
     * 0x42 "BD-R random recording"
     *Empty drives are supposed to report
     * 0x00 ""
     */
    bool getDiscType(struct burn_drive* drive, int& discType, QString& discTypeName);

    //! Получить тип диска
    /*!
     * \brief Получить тип диска и описание типа в выбранном приводе
     * \param driveName полное имя устройства из карты mDrivesNamesMap
     * \param discType тип диска (значения см. ниже)
     * \param discTypeName строка с типом диска (значения см. выше)
     * \return в случае успешного получения информации true, в противном случае false
     *
     * Устройство должно быть захвачено перед вызовом данной функции
     */
    bool getDiscType(const QString driveName, int& discType, QString& discTypeName);

    //! Стираемый ли диск
    /*!
     * \brief Стираемый ли диск
     * \param drive устройство привода
     * \return если диск стираемый, то вернет true, если нет то false
     *
     * Устройство должно быть захвачено перед вызовом данной функции
     * Вызывать метод только после получения статуса диска BURN_DISC_FULL
     */
    bool isDiscErasable(struct burn_drive* drive);

    //! Стираемый ли диск
    /*!
     * \brief Стираемый ли диск
     * \param driveName полное имя устройства из карты mDrivesNamesMap
     * \return если диск стираемый, то вернет true, если нет то false
     *
     * Устройство должно быть захвачено перед вызовом данной функции
     * Вызывать метод только после получения статуса диска BURN_DISC_FULL
     */
    bool isDiscErasable(const QString driveName);

    //! Максимальная скорость записи привода на вставленный в него диск в Кб/с (К - 1000, а не 1024)
    /*!
     * \brief Максимальная скорость записи привода на вставленный в него диск
     * \param drive устройство привода
     * \return скорость, в Кб/с (К - 1000, а не 1024)
     *
     * Рекомендуется захватить устройство перед вызовом данной функции
     */
    //int getMaxWriteSpeed(struct burn_drive* drive);

    //! Максимальная скорость записи привода на вставленный в него диск в Кб/с (К - 1000, а не 1024)
    /*!
     * \brief Максимальная скорость записи привода на вставленный в него диск
     * \param driveName полное имя устройства из карты mDrivesNamesMap
     * \return скорость, в Кб/с (К - 1000, а не 1024)
     *
     * Рекомендуется захватить устройство перед вызовом данной функции
     */
    //int getMaxWriteSpeed(const QString driveName);

    //! Минимальная скорость записи привода на вставленный в него диск в Кб/с (К - 1000, а не 1024)
    /*!
     * \brief Минимальная скорость записи привода на вставленный в него диск
     * \param drive устройство привода
     * \return скорость, в Кб/с (К - 1000, а не 1024)
     *
     * Рекомендуется захватить устройство перед вызовом данной функции
     */
    //int getMinWriteSpeed(struct burn_drive* drive);

    //! Минимальная скорость записи привода на вставленный в него диск в Кб/с (К - 1000, а не 1024)
    /*!
     * \brief Минимальная скорость записи привода на вставленный в него диск
     * \param driveName полное имя устройства из карты mDrivesNamesMap
     * \return скорость, в Кб/с (К - 1000, а не 1024)
     *
     * Рекомендуется захватить устройство перед вызовом данной функции
     */
    //int getMinWriteSpeed(const QString driveName);

    //! Максимальная скорость чтения привода в Кб/с (К - 1000, а не 1024)
    /*!
     * \brief Максимальная скорость чтения привода
     * \param drive устройство привода
     * \return скорость, в Кб/с (К - 1000, а не 1024)
     *
     * Рекомендуется захватить устройство перед вызовом данной функции
     */
    //int getMaxReadSpeed(struct burn_drive* drive);

    //! Максимальная скорость чтения привода в Кб/с (К - 1000, а не 1024)
    /*!
     * \brief Максимальная скорость чтения привода
     * \param driveName полное имя устройства из карты mDrivesNamesMap
     * \return скорость, в Кб/с (К - 1000, а не 1024)
     *
     * Рекомендуется захватить устройство перед вызовом данной функции
     */
    //int getMaxReadSpeed(const QString driveName);

    //! Доступная свободная емкость диска в байтах
    /*!
     * \brief Доступная свободная емкость диска
     * \param drive устройство привода
     * \param opts параметры записи: если не заданы, то используются установленные до вызова этой функции
     * \return свободное место в байтах
     *
     * Устройство должно быть захвачено перед вызовом данной функции
     */
    off_t getAvailableDiscSpace(struct burn_drive* drive);

    //! Доступная свободная емкость диска в байтах
    /*!
     * \brief Доступная свободная емкость диска
     * \param driveName полное имя устройства из карты mDrivesNamesMap
     * \return свободное место в байтах
     *
     * Устройство должно быть захвачено перед вызовом данной функции
     */
    off_t getAvailableDiscSpace(const QString driveName);


    //! Отправить сообщение (сигнал) с текущим статусом привода
    /*!
     * \brief Отправить сообщение (сигнал) с текущим статусом привода
     * \param driveStatus текущий статус привода
     */
    inline void sendCurrentDriveStatus(const burn_drive_status& driveStatus);

    //! Копирование содержимого каталога
    /*!
     * \brief Копирование содержимого каталога. При необходимости создает каталог-приемник
     * \param src каталог с копируемым содержимым
     * \param dst каталог куда копируется
     * \return true в случае успеха, false в случае ошибки
     */
    //bool copyDir(QString srcDirPath, QString dstDirPath);

    //! Удаление содержимого каталога
    /*!
     * \brief Удаление содержимого каталога
     * \param dir каталог для удаления
     * \return true в случае успеха, false в случае ошибки
     */
    //bool removeDir(const QString &dirPath);
};

/*!
    \struct DiscInfo
    \brief Структура для получения информации о диске/

    На момент получения информации она уже может быть неактуальной
*/
struct DiscInfo
{
    //! Конструктор по умолчанию
    DiscInfo();

    //! Конструктор
    /*!
     * \brief Конструктор для получения информации о диске
     * \param driveName полное имя устройства из списка устройств, которое возвращает DiscRec::drivesList()
     */
    DiscInfo(QString driveName);

    //! Получить имя устройства
    /*!
     * \brief Получить имя устройства
     * \return имя устройства
     */
    QString driveName();

    //! Валидна ли полученная информация
    /*!
     * \brief Валидна ли полученная информация
     * \return в случае если информация валидна вернет true, в противном случае false
     *
     * Рекомендуется сразу после получения экземпляра структуры проверить её валидность при помощи данного метода.
     * В любое время информация может стать устаревшей.
     */
    bool isValid() const;

    //! Объявить не валидной информацию о диске
    /*!
     * \brief Объявить не валидной информацию о диске
     */
    void invalid();

    //! Обновить информацию, выполнив повторные запросы к библиотеке
    /*!
     * \brief обновление информации
     * \return в случае успеха получения true, в противном случае false
     */
    bool update();

    //! Получить тип диска
    /*!
     * \brief Получить тип диска и описание типа в выбранном приводе
     * \param discType тип диска (значения см. в DiscRec::getDiscType())
     * \param discTypeName строка с типом диска (значения см. в DiscRec::getDiscType())
     * \return в случае успешного получения информации true, в противном случае false
     */
    bool getDiscType(int& discType, QString& discTypeName) const;

    //! Получить статус диска
    /*!
     * \brief Получить статус диска в выбранном приводе
     * \return статус диска (значения см. в DiscRec::getDiscStatus()) или BURN_DISC_UNREADY в случае ошибки или прерывания выполнения
     */
    enum burn_disc_status getDiscStatus() const;

    //! Получить информацию о форматировании диска
    /*!
     * \brief Функция для получения информации о форматировании диска, была ли она произведена и т.п.
     * \param formatStatus статус форматирования диска
     * \param numFormats количество доступных форматирований
     * \param formatSize размер после форматирования
     * \return в случае успешного получения информации true, в противном случае false
     */
    bool getDiscFormatType(int& formatStatus, int& numFormats, off_t& formatSize) const;

    //! Получить статус привода
    /*!
     * \brief Получить статус выбранного привода
     * \return статус привода (значения см. в DiscRec::getDriveStatus())
     */
    enum burn_drive_status getDriveStatus() const;

    //! Стираемый ли диск
    /*!
     * \brief Стираемый ли диск
     * \return если диск стираемый, то вернет true, если нет то false
     */
    bool isDiscErasable() const;

    //! Максимальная скорость записи привода на вставленный в него диск в Кб/с (К - 1000, а не 1024)
    /*!
     * \brief Максимальная скорость записи привода на вставленный в него диск
     * \return скорость, в Кб/с (К - 1000, а не 1024)
     */
    int getMaxWriteSpeed() const;

    //! Минимальная скорость записи привода на вставленный в него диск в Кб/с (К - 1000, а не 1024)
    /*!
     * \brief Минимальная скорость записи привода на вставленный в него диск
     * \return скорость, в Кб/с (К - 1000, а не 1024)
     */
    int getMinWriteSpeed() const;

    //! Максимальная скорость чтения привода в Кб/с (К - 1000, а не 1024)
    /*!
     * \brief Максимальная скорость чтения привода
     * \return скорость, в Кб/с (К - 1000, а не 1024)
     */
    int getMaxReadSpeed() const;

    //! Доступная свободная емкость диска в байтах
    /*!
     * \brief Доступная свободная емкость диска
     * \return свободное место в байтах
     */
    off_t getAvailableDiscSpace() const;

private:
    QString mDriveName;                 //!< полное имя устройства из списка устройств, которое возвращает DiscRec::drivesList()
    bool mIsValid;                      //!< валидна ли и актуальна ли информация
    int mDiscType;                      //!< типа диска в устройстве
    QString mDiscTypeName;              //!< текст с описанием типа диска в устройстве
    burn_disc_status mDiscStatus;       //!< статус диска в устройстве
    int mFormatStatus;                  //!< статус форматирования диска
    int mNumFormats;                    //!< количество доступных форматирований
    off_t mFormatSize;                  //!< размер после форматирования
    burn_drive_status mDriveStatus;     //!< статус привода
    //struct burn_progress mBurnProgress; //!< прогресс выполнения операции приводом (операции mDriveStatus)
    bool mIsDiscErasable;               //!< стираемый ли диск
    int mMaxWriteSpeed;                 //!< максимальная скорость записи привода на вставленный в него диск в Кб/с (К - 1000, а не 1024)
    int mMinWriteSpeed;                 //!< минимальная скорость записи привода на вставленный в него диск в Кб/с (К - 1000, а не 1024)
    int mMaxReadSpeed;                  //!< максимальная скорость чтения привода в Кб/с (К - 1000, а не 1024)
    off_t mAvailableDiscSpace;          //!< доступная свободная емкость диска в байтах
};

/*!
    \struct DriveInfoConst
    \brief Структура для получения информации о приводе (постоянная, неменяющаяся информация)
*/
struct DriveInfoConst
{
    //! Конструктор по умолчанию
    DriveInfoConst();

    //! Конструктор
    /*!
     * \brief Конструктор для получения информации о устройстве
     * \param driveName полное имя устройства из списка устройств, которое возвращает DiscRec::drivesList()
     */
    DriveInfoConst(QString driveName);

    //! Диск DVD-RAM?
    /*!
     * \brief Диск DVD-RAM?
     * \return если диск DVD-RAM, то вернет true, иначе false
     */
    bool driveIsDvdRam();

    //! Диск DVD-R+?
    /*!
     * \brief Диск DVD-R+?
     * \return если диск DVD-R+, то вернет true, иначе false
     */
    bool driveIsDvdRPlus();

    //! Диск DVD-R-?
    /*!
     * \brief Диск DVD-R-?
     * \return если диск DVD-R-, то вернет true, иначе false
     */
    bool driveIsDvdRMinus();

    //! Диск DVD-RW-?
    /*!
     * \brief Диск DVD-RW-?
     * \return если диск DVD-RW-, то вернет true, иначе false
     */
    bool driveIsDvdRWMinus();

    //! Диск DVD-RW+?
    /*!
     * \brief Диск DVD-RW+?
     * \return если диск DVD-RW+, то вернет true, иначе false
     */
    bool driveIsDvdRWPlus();

    //! Диск CD-R?
    /*!
     * \brief Диск CD-R?
     * \return если диск CD-R, то вернет true, иначе false
     */
    bool driveIsCdR();

    //! Диск CD-RW?
    /*!
     * \brief Диск CD-RW?
     * \return если диск CD-RW, то вернет true, иначе false
     */
    bool driveIsCdRW();

    //! Диск BD-R?
    /*!
     * \brief Диск BD-R?
     * \return если диск BD-R, то вернет true, иначе false
     */
    bool driveIsBdR();

    //! Получить имя устройства
    /*!
     * \brief Получить имя устройства
     * \return имя устройства
     */
    QString driveName();

    //! Получить вендора выбранного привода
    /*!
     * \brief Получить вендора (название производителя) привода
     * \return вендор привода
     */
    QString driveVendor();

    //! Получить модель выбранного привода
    /*!
     * \brief Получить модель привода
     * \return модель привода
     */
    QString driveProduct();

    //! Получить ревизию выбранного привода
    /*!
     * \brief Получить ревизию (версию) привода
     * \return ревизия привода
     */
    QString driveRevision();

    //! Получить имя устройства в /dev выбранного привода
    /*!
     * \brief Получить местоположение в системе привода
     * \return устройство привода в /dev
     */
    QString driveLocation();



private:
    QString mDriveName; //!< полное имя устройства из списка устройств, которое возвращает DiscRec::drivesList()
};

#endif // DISCREC_H
