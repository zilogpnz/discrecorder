#ifndef COMMON_H
#define COMMON_H

#include <QString>

//! Константы
namespace Const
{
    const QString backupDir("/backup/");
    const QString metaFile("metadata.json");
    const QString excludeFile("/mnt/syspart/tmp/exclude");
    const QString isoPath("/mnt/syspart/tmp/tmp.iso");          //!< путь к создаваемому ISO-образу
//    const QString loaderArc("/mnt/syspart/var/restorecd.tar");  //!< Путь к архиву с шаблоном загрузочного диска
    const QString discTitle("restore");                         //!< имя диска
//    const bool isRecordEmulation = false;                       //!< эмуляция записи (true) или реальная запись (false)
//    const bool isEjectDisc = false;                         //!< извлекать ли диск после работы с операциями, связанными с записью (окончание, ошибка, неподходящий диск)
//    const bool isInjectDiscWhileGrubbig = false;            //!< задвигать ли лоток при выполнении захвата у-ва
//    const int usecSleepDrvDiscStatus = 1000;                //!< время в микросекундах для ожидания в циклах получения информации о статусе привода и диска
//    const int usecSleepBurnDiscStatus = 1000 * 100;         //!< время в микросекундах для ожидания в цикле получения информации о прогрессе записи диска
    const int msecWaitThreadTime = 3000;                    //!< время в миллисекундах сколько ожидаем завершения процесса перед тем как его терминейтить принудительно в случае зависания
    const int minIsoSize = 1 * 1024 * 1024;                 //!< минимально необходимый размер для запси диска
}

/*!
    \class BaseSingletone
    \brief Базовый класс (интерфейс) для объектов, которые должны иметь один экземпляр

    В многопоточном приложении вызвать instance().initialize() перед использованием синглтона в разных потоках
    (сделано чтобы не лепить мьютекс в instance())
*/
template <class T>
class BaseSingletone
{
public:
    //! Получение экземпляра
    static T& instance()
    {
        static T inst;
        return inst;
    };

    //! Инициализация
    /*!
        Вызывать метод перед созданием потоков
        В случае удачной инициализации возвращать true,
        в противном случае false
    */
    virtual bool initialize() = 0;

    //! Завершение работы
    /*!
        Вызывать метод после того, как синглтон стал ненужным
        В случае удачного завершения работы возвращать true,
        в противном случае false
    */
    virtual bool shutdown() = 0;

protected:
    BaseSingletone() {};
    virtual ~BaseSingletone() {};

private:
    BaseSingletone(const BaseSingletone&);
    BaseSingletone& operator=(const BaseSingletone&);
};

#endif // COMMON_H
