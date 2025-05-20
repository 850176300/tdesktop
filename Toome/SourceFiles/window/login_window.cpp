#include "login_window.h"
#include "ui/painter.h"
#include "core/application.h"
#include <QtGui/QScreen>
namespace Window{

LoginWindow::LoginWindow()
    :Platform::SimpleWindow(){
    setLocale(QLocale(QLocale::English, QLocale::UnitedStates));

    _loginBg.load(u":/gui/art/bg_login.jpg"_q);
    setFixedSize(QSize(464,650));
    setWindowFlag(Qt::FramelessWindowHint);
    setTitleVisbile(false);

    setAttribute(Qt::WA_OpaquePaintEvent);
}

void LoginWindow::initHook() {
    Platform::SimpleWindow::initHook();
    QCoreApplication::instance()->installEventFilter(this);
}

void LoginWindow::paintEvent(QPaintEvent *e){
    
    QPainter p(this);
    const auto hq = PainterHighQualityEnabler(p);
    p.drawImage(e->rect(), _loginBg);
}

void LoginWindow::closeEvent(QCloseEvent* e) {
    e->accept();
    Core::Quit();
}

void LoginWindow::finishFirstShow(){
    setPositionInited();
    setAttribute(Qt::WA_NoSystemBackground);
}

}