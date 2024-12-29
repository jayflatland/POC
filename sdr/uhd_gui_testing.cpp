/*
Tasks:
[ ] Figure out app close error
[ ] Figure out proper buffer sequencing
    [ ] Get samples in order
[ ] Record samples for analysis
[ ] Get audio output working
[ ] Get a simple FM demodulator working
*/

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
    static constexpr int samps_per_buff = (1<<20);
    float samples_[samps_per_buff];

    OpenGLWidget(QWidget *parent = nullptr) : QOpenGLWidget(parent)
    {
        usrp = uhd::usrp::multi_usrp::make("name,product=B200,serial=F5C23F,type=b200,uhd");
        // usrp = uhd::usrp::multi_usrp::make("serial=11079075318916476466,driver=audio,type=soapy");

        if(!usrp) { exit(0); }

        usrp->set_rx_antenna("TX/RX", 0);
        usrp->set_rx_rate(60e6, uhd::usrp::multi_usrp::ALL_CHANS);
        // usrp->set_rx_freq(101.1e6, 0);//101 the fox
        // usrp->set_rx_freq(151.88e6, 0);//Jacob
        // usrp->set_rx_freq(89.3e6, 0);//NPR
        usrp->set_rx_freq(92.3e6, 0);//Nearby tower
        // usrp->set_rx_freq(920e6, 0);//random 900MHz stuff
        // usrp->set_rx_freq(2.45e9, 0);//2.4GHz
        usrp->set_rx_bandwidth(200e3, 0);

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

    virtual ~OpenGLWidget()
    {
        for(int i = 0; i < (int)buffs.size(); i++)
        {
            delete buffs[i];
        }
    }

protected:
    uhd::usrp::multi_usrp::sptr usrp;
    uhd::rx_streamer::sptr rx_stream;
    std::vector<float*> buffs;
    float filtered_max = 0.01;

    void initializeGL() override {
        initializeOpenGLFunctions();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // Set clear color to black
    }

    void resizeGL(int w, int h) override {
        glViewport(0, 0, w, h);
    }

    void paintGL() override {
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_MORE);
        stream_cmd.num_samps  = samps_per_buff;
        stream_cmd.stream_now = true;
        // stream_cmd.time_spec  = usrp->get_time_now() + uhd::time_spec_t(0.5);
        rx_stream->issue_stream_cmd(stream_cmd);

        
        // float *dst = samples_;
        // float *end = samples_ + samps_per_buff;
        while(true)
        {
            uhd::rx_metadata_t md;
            size_t num_rx_samps;
            // static int cnt = 0;
            num_rx_samps = rx_stream->recv(buffs, samps_per_buff, md, 3.0, true);
            (void)num_rx_samps;
            // std::cout << "got " << num_rx_samps << " samples" << std::endl;
            if(num_rx_samps == 0)
            {
                break;
            }
            // cnt++;
            // const float *src = buffs[0];
            // for(int i = 0; i < num_rx_samps; i++)
            // {
            //     *dst++ = *src++;
            //     if(dst >= end) { break; }
            // }
        }

        static constexpr int draw_samps = 4000;

        float this_max = 0.0;
        for(int i = 0; i < draw_samps; i++)
        {
            this_max = std::max(this_max, buffs[0][i]);
        }
        filtered_max = filtered_max * 0.99 + this_max * 0.01;
        // filtered_max = 1.0;

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, 1.0, -filtered_max * 5.0, filtered_max * 5.0, -1.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        // gluOrtho2D(0, 0, 800, 600);

        glClearColor(0.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);

        float x, y;

        glColor3f(1.0, 0.0, 0.0);
        glBegin(GL_LINE_STRIP);
        x = 0.0;
        for(int i = 0; i < draw_samps; i += 2)
        {
            y = buffs[0][i];
            glVertex2f(x, y);
            x += 2.0 / draw_samps;
        }
        glEnd();

        glColor3f(0.0, 1.0, 0.0);
        glBegin(GL_LINE_STRIP);
        x = 0.0;
        for(int i = 0; i < draw_samps; i += 2)
        {
            y = buffs[0][i + 1];
            glVertex2f(x, y);
            x += 2.0 / draw_samps;
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
