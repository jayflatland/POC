#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>

class OpenGLWidget : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    OpenGLWidget(QWidget *parent = nullptr) : QOpenGLWidget(parent) {}

protected:
    void initializeGL() override {
        initializeOpenGLFunctions();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // Set clear color to black
    }

    void resizeGL(int w, int h) override {
        glViewport(0, 0, w, h);
    }

    void paintGL() override {
        glClear(GL_COLOR_BUFFER_BIT);

        // Example rendering: draw a simple triangle
        glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.0f, 0.0f); glVertex2f(0.0f, 1.0f); // Top vertex (red)
        glColor3f(0.0f, 1.0f, 0.0f); glVertex2f(-1.0f, -1.0f); // Bottom-left vertex (green)
        glColor3f(0.0f, 0.0f, 1.0f); glVertex2f(1.0f, -1.0f); // Bottom-right vertex (blue)
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
