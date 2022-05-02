#include <Windows.h>
#include <opencv2/opencv.hpp>

using namespace cv;


int main()//int argc, char *argv[])
{
    XColor c;
    Display *rootDisplay = XOpenDisplay(nullptr);
    int rootDisplayScreen = XDefaultScreen(rootDisplay);
    Window rootWindow = XRootWindow(rootDisplay, rootDisplayScreen);
    Colormap rootColormap = XDefaultColormap(rootDisplay, rootDisplayScreen);

    int x=0;
    int y=0;

    XImage *image = XGetImage(rootDisplay, rootWindow, x, y, 1, 1, AllPlanes, XYPixmap);
    c.pixel = XGetPixel(image, 0, 0);
    XFree(image);

    XQueryColor(rootDisplay, rootColormap, &c);

    std::cout << c.red/256 << " " << c.green/256 << " " << c.blue/256 << "\n";

    return 0;
}