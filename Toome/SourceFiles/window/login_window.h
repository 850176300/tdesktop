#pragma once

#include "platform/platform_main_window.h"
#include "base/object_ptr.h"
#include <QtGui/QImage>


namespace Window {

class LoginWindow: public Platform::SimpleWindow
{
public:
	explicit LoginWindow();
	~LoginWindow() = default;
protected:
	void paintEvent(QPaintEvent *e) override;
	void initHook() override;
	void closeEvent(QCloseEvent* e) override;
public:
	void finishFirstShow();
private:
	QImage _loginBg;
};

    
}
