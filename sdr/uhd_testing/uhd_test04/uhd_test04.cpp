#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QTimer>

#include <uhd/convert.hpp>
#include <uhd/exception.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#ifdef __linux__
#    include <boost/filesystem.hpp>
#    include <boost/process.hpp>
#endif
#include <chrono>
#include <complex>
#include <csignal>
#include <fstream>
#include <iostream>
#include <numeric>
#include <regex>
#include <thread>

class OpenGLWidget : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    static const int samps_per_buff = (1<<20);

    OpenGLWidget(QWidget *parent = nullptr) : QOpenGLWidget(parent)
    {
        usrp = uhd::usrp::multi_usrp::make("name,product=B200,serial=F5C23F,type=b200,uhd");
        if(!usrp)
        {
            exit(0);
        }

        usrp->set_rx_rate(8e6, uhd::usrp::multi_usrp::ALL_CHANS);
        //usrp->set_rx_freq(101.1e6, 0);
        usrp->set_rx_freq(900.0e6, 0);
        //usrp->set_rx_gain(1.0, uhd::usrp::multi_usrp::ALL_CHANS);
        usrp->set_rx_bandwidth(8e6, 0);
        // usrp->set_rx_antenna("RX/TX", 0);

        uhd::stream_args_t stream_args("fc32", "sc16");
        stream_args.channels = {0};
        rx_stream = usrp->get_rx_stream(stream_args);
        if(!rx_stream)
        {
            exit(0);
        }

        buffs.resize(rx_stream->get_num_channels());
        for(int i = 0; i < (int)buffs.size(); i++)
        {
            buffs[i] = new float[samps_per_buff];
        }

        // Set up a timer to trigger continuous redrawing
        QTimer *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, QOverload<>::of(&OpenGLWidget::update));
        timer->start(16); // Approximately 60 FPS

    }

protected:
    uhd::usrp::multi_usrp::sptr usrp;
    uhd::rx_streamer::sptr rx_stream;
    std::vector<float*> buffs;

    void initializeGL() override {
        initializeOpenGLFunctions();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // Set clear color to black
    }

    void resizeGL(int w, int h) override {
        glViewport(0, 0, w, h);
    }

    void paintGL() override {
        // glClear(GL_COLOR_BUFFER_BIT);

        // // Example rendering: draw a simple triangle
        // glBegin(GL_TRIANGLES);
        // glColor3f(1.0f, 0.0f, 0.0f); glVertex2f(0.0f, 1.0f); // Top vertex (red)
        // glColor3f(0.0f, 1.0f, 0.0f); glVertex2f(-1.0f, -1.0f); // Bottom-left vertex (green)
        // glColor3f(0.0f, 0.0f, 1.0f); glVertex2f(1.0f, -1.0f); // Bottom-right vertex (blue)
        // glEnd();

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, 4000, -0.01, 0.01, -1.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        // gluOrtho2D(0, 0, 800, 600);

        glClearColor(0.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);

        glColor3f(1.0, 0.0, 1.0);
        // glBegin(GL_TRIANGLES);
        // glVertex3f(400 + -100.0, 400 + -100.0, 0.0);
        // glVertex3f(400 + 100.0, 400 + -100.0, 0.0);
        // glVertex3f(400 + 100.0, 400 + 100.0, 0.0);
        // glEnd();

        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_MORE);
        stream_cmd.num_samps  = samps_per_buff;
        stream_cmd.stream_now = true;
        // stream_cmd.time_spec  = usrp->get_time_now() + uhd::time_spec_t(0.5);
        rx_stream->issue_stream_cmd(stream_cmd);

        uhd::rx_metadata_t md;
        size_t num_rx_samps;
        // static int cnt = 0;
        num_rx_samps = rx_stream->recv(buffs, samps_per_buff, md, 3.0, true);
        (void)num_rx_samps;
        // std::cout << cnt << ": got " << num_rx_samps << " samples" << std::endl;
        // cnt++;

        glBegin(GL_LINE_STRIP);
        float x = 0.0;
        float y;
        for(int i = 0; i < std::min(4000, samps_per_buff); i++)
        {
            y = buffs[0][i];
            glVertex2f(x, y);
            x += 1.0;
        }
        glEnd();

    }
};

class FullScreenApp : public QMainWindow {
public:
    FullScreenApp() {
        // Set the window title
        setWindowTitle("Fullscreen Qt Application");

        // Create a menu bar
        QMenuBar *menuBar = new QMenuBar(this);

        // Add menus to the menu bar
        QMenu *fileMenu = menuBar->addMenu("&File");
        QMenu *editMenu = menuBar->addMenu("&Edit");
        QMenu *helpMenu = menuBar->addMenu("&Help");
        (void)editMenu;
        (void)helpMenu;

        // Add actions to the File menu
        fileMenu->addAction("Open", []() { /* Add Open functionality */ });
        fileMenu->addAction("Save", []() { /* Add Save functionality */ });
        fileMenu->addSeparator();
        fileMenu->addAction("E&xit", [this]() { close(); });

        // Set the menu bar for the main window
        setMenuBar(menuBar);

        // Create an OpenGL widget
        OpenGLWidget *glWidget = new OpenGLWidget();
        setCentralWidget(glWidget);

        // Maximize the application window but keep the title bar
        showMaximized();
    }
};

int main(int argc, char *argv[]) {
    // Initialize the Qt application
    QApplication app(argc, argv);

    // Create and show the main application window
    FullScreenApp window;

    // Run the application
    return app.exec();
}
