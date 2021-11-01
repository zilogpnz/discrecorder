#include <QApplication>
#include <QTextCodec>
#include <QtGlobal>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    if(argc<2) return 1;

    QApplication a(argc, argv);

#if QT_VERSION < 0x050000
    //Установка кодека для правильного отображения русских символов
    QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
#endif

    MainWindow w;
    w.show();

    return a.exec();
}
