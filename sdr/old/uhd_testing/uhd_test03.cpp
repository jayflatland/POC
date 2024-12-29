#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <GL/glut.h>

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

static const int SCREEN_WIDTH = 3400;
static const int SCREEN_HEIGHT = 2000;
static const int samps_per_buff = (1<<20);

static uhd::usrp::multi_usrp::sptr usrp;
static uhd::rx_streamer::sptr rx_stream;
static std::vector<float*> buffs;

static void Key(unsigned char key, int x, int y)
{
    switch (key)
    {
    case 27:
        exit(0);
    }
}

static void Draw(void)
{
    // glViewport(0, 0, 100, 100);
    // glMatrixMode(GL_PROJECTION);
    // glLoadIdentity();
    // // gluPerspective(60.0, 1.0, 0.1, 1000.0);
    // gluOrtho2D(0.0, 100.0, 100.0, 0.0);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 1000, -0.01, 0.01, -1.0, 1.0);

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
    static int cnt = 0;
    num_rx_samps = rx_stream->recv(buffs, samps_per_buff, md, 3.0, true);
    std::cout << cnt << ": got " << num_rx_samps << " samples" << std::endl;
    cnt++;

    glBegin(GL_LINE_STRIP);
    float x = 0.0;
    float y;
    for(int i = 0; i < std::min(1000, samps_per_buff); i++)
    {
        y = buffs[0][i];
        glVertex2f(x, y);
        x += 1.0;
    }
    glEnd();

    glutSwapBuffers();
    glutPostRedisplay();
}

int main(int argc, char **argv)
{
    usrp = uhd::usrp::multi_usrp::make("name,product=B200,serial=F5C23F,type=b200,uhd");
    if(!usrp) { return 0; }

    usrp->set_rx_rate(8e6, uhd::usrp::multi_usrp::ALL_CHANS);
    usrp->set_rx_freq(101.1e6, 0);
    //usrp->set_rx_gain(1.0, uhd::usrp::multi_usrp::ALL_CHANS);
    usrp->set_rx_bandwidth(8e6, 0);
    // usrp->set_rx_antenna("RX/TX", 0);

    uhd::stream_args_t stream_args("fc32", "sc16");
    stream_args.channels = {0};
    rx_stream = usrp->get_rx_stream(stream_args);
    if(!rx_stream) { return -1; }

    buffs.resize(rx_stream->get_num_channels());
    for(int i = 0; i < buffs.size(); i++)
    {
        buffs[i] = new float[samps_per_buff];
    }

    glutInit(&argc, argv);

    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
    glutInitWindowSize(SCREEN_WIDTH, SCREEN_HEIGHT);
    glutCreateWindow("UHD Stuff");
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    // glutFullScreen();

    glutKeyboardFunc(Key);
    glutDisplayFunc(Draw);
    glutMainLoop();
    return 0; /* ANSI C requires main to return int. */
}
