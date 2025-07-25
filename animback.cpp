
#include <Application.h>
#include <Window.h>
#include <View.h>

#include <Button.h>
#include <TextControl.h>
#include <StringView.h>
#include <Alert.h>
#include <Slider.h>

#include <LayoutBuilder.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>
#include <FindDirectory.h>
#include <Node.h>
#include <kernel/fs_attr.h>

#include <Message.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <be_apps/Tracker/Background.h>

#include <Screen.h>
#include <InterfaceDefs.h>

#include <vector>
#include <string>
#include <algorithm>
#include <iostream>


const uint32 SET_BACKGROUND     = 'SETB';
const uint32 CLEAR_BACKGROUND   = 'CLRB';
const uint32 REFRESH_BACKGROUND = 'REFB';
const uint32 START_ANIMATION    = 'STAR';
const uint32 STOP_ANIMATION     = 'STOP';
const uint32 ANIMATE_FRAME      = 'FRAM';
const uint32 SPEED_CHANGE       = 'SPED';

class BGSWindows;

class BGSWindow: public BWindow {
public:
    BGSWindow();
    virtual void MessageReceived(BMessage* message);
    virtual bool exitReq();
private:
    bool isValidDir(const char* path);
    status_t setWallpaper(const char* imagePath);
    void refreshTracker();
    void showStatus(const char* message);
    bool loadAnimationFrames(const char* message);

    void startAnimation();
    void stopAnimation();
    void nextFrame();
    void updateAnimControls();

    BTextControl* pathInput;
    BStringView* statusLabel;
    BSlider* fpsSlider;
    BButton* setButton;
    BButton* clearButton;
    BButton* refreshButton;
    BButton* startButton;
    BButton* stopButton;

    std::vector<std::string> frameFiles;
    int32 currentFrame;
    BMessageRunner* animationRunner;

    bigtime_t delay;
    bool isPlaying;

    std::string animationFolder;
};

class BGSApp: public BApplication {
public:
    BGSApp(bool cli_mode = false): BApplication("application/x-vnd.cell-animBack"), cliMode(cli_mode) {}
    virtual void ReadyToRun() {
        if (!cliMode) {
            window = new BGSWindow();
            window->Show();
        }
    }
    bool isValidDir(const char* path);
    status_t setWallpaper(const char* imagePath);
    void refreshTracker();
    bool loadAnimationFrames(const char* folderPath);
    void startCliAnimation(const char* path, int fps);
private:
    BGSWindow* window;
    bool cliMode;
    std::vector<std::string> frameFiles;
    int32 currentFrame;
    bigtime_t delay;
};

BGSWindow::BGSWindow(): BWindow(
    BRect(50, 50, 900, 450),
                                "animBack",
                                B_TITLED_WINDOW,
                                B_QUIT_ON_WINDOW_CLOSE),
currentFrame(0),
animationRunner(nullptr),
delay(100000),
isPlaying(false) {

    pathInput = new BTextControl("path", "Path:", "", new BMessage(SET_BACKGROUND));
    pathInput->SetDivider(80);
    setButton     = new BButton("set", "Load", new BMessage(SET_BACKGROUND));
    clearButton   = new BButton("clear", "Clear", new BMessage(CLEAR_BACKGROUND));
    refreshButton = new BButton("refresh", "Refresh", new BMessage(REFRESH_BACKGROUND));
    startButton   = new BButton("start", "Start Animation", new BMessage(START_ANIMATION));
    stopButton    = new BButton("stop", "Stop Animation", new BMessage(STOP_ANIMATION));

    fpsSlider = new BSlider("speed", "Animation Speed (FPS):", new BMessage(SPEED_CHANGE), 1, 30, B_HORIZONTAL);
    fpsSlider->SetValue(10);
    fpsSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
    fpsSlider->SetHashMarkCount(10);
    fpsSlider->SetTarget(this);

    statusLabel = new BStringView("status", "Enter a path to the folder, frames must be numbered like 1.png, 2.png, 3.png. etc etc.");

    BLayoutBuilder::Group<>(this, B_VERTICAL, 10)
    .SetInsets(10)
    .Add(pathInput)
    .AddGroup(B_HORIZONTAL, 10)
    .Add(setButton)
    .Add(clearButton)
    .Add(refreshButton)
    .AddGlue()
    .End()
    .Add(fpsSlider)
    .AddGroup(B_HORIZONTAL, 10)
    .Add(startButton)
    .Add(stopButton)
    .AddGlue()
    .End()
    .Add(statusLabel)
    .AddGlue();
    updateAnimControls();
}

bool BGSWindow::exitReq() {
    stopAnimation();
    return BGSWindow::exitReq();
}

void BGSWindow::MessageReceived(BMessage* message) {
    switch (message->what) {
        case SET_BACKGROUND: {
            const char* path = pathInput -> Text();
            if (!isValidDir(path)) {
                showStatus("Invalid dir, maybe doesn't exist");
                break;
            }

            if (!loadAnimationFrames(path)) {
                showStatus("No valid numbered frames found (1.png, 2.png, ...)");
                break;
            }

            animationFolder = std::string(path);
            showStatus("Animation frames loaded. Click \'Start Animation\' to commence.");
            updateAnimControls();

            break;
        }

        case CLEAR_BACKGROUND: {
            if (setWallpaper("") == B_OK) {
                showStatus("cleared");
                pathInput -> SetText("");
                refreshTracker();
            } else {
                showStatus("Clearing failed?");
            }
            break;
        }

        case REFRESH_BACKGROUND: {
            refreshTracker();
            showStatus("Refreshed!");
            break;
        }

        case START_ANIMATION: {
            if (!frameFiles.empty()) {
                startAnimation();
            } else {
                showStatus("No animation frames loaded?");
            }
            break;
        }

        case STOP_ANIMATION: {
            stopAnimation();
            break;
        }

        case ANIMATE_FRAME: {
            nextFrame();
            break;
        }

        case SPEED_CHANGE: {
            int32 fps = fpsSlider -> Value();
            delay = 1000000/fps;

            if (isPlaying) {
                stopAnimation();
                startAnimation();
            }
            break;
        }
        default: BWindow::MessageReceived(message); break;
    }
}

bool BGSWindow::isValidDir(const char* path) {
    if (strlen(path) == 0) return false;
    BEntry entry(path);

    return (entry.InitCheck() == B_OK && entry.Exists() && entry.IsDirectory());
}

bool BGSWindow::loadAnimationFrames(const char* folderPath) {
    frameFiles.clear();
    currentFrame = 0;

    BDirectory dir(folderPath);
    if (dir.InitCheck() != B_OK) {
        return false;
    }

    std::vector<std::pair<int, std::string>> numberedFiles;

    BEntry entry;

    while (dir.GetNextEntry(&entry) == B_OK) {
        if (!entry.IsFile()) continue;

        char name[B_FILE_NAME_LENGTH];
        entry.GetName(name);

        std::string filename(name);
        size_t dp = filename.find_last_of('.');
        if (dp == std::string::npos) continue;

        std::string numberPart = filename.substr(0, dp);
        std::string extension =  filename.substr(dp + 1);

        if (extension != "png" && extension != "jpg" && extension != "jpeg" &&
            extension != "bmp" && extension != "gif" && extension != "tiff") { continue; }
            bool isNumeric = true;

        for (char c : numberPart) {
            if (!isdigit(c)) {
                isNumeric = false;
                break;
            }}

            if (isNumeric && !numberPart.empty()) {
                int frameNumber = std::stoi(numberPart);
                BPath fullPath;
                entry.GetPath(&fullPath);
                numberedFiles.push_back({frameNumber, std::string(fullPath.Path())});
            }
    }


    if (numberedFiles.empty()) {
        return false;
    }

    std::sort(numberedFiles.begin(), numberedFiles.end());

    for (const auto& pair : numberedFiles) {
        frameFiles.push_back(pair.second);
    }

    return true;

}

void BGSWindow::startAnimation() {
    if (frameFiles.empty() || isPlaying) {return;}

    isPlaying = true;
    currentFrame = 0;

    BMessage animMsg(ANIMATE_FRAME);
    animationRunner = new BMessageRunner(this, &animMsg, delay);

    updateAnimControls();
    showStatus("Playing animation!");

    nextFrame();
}

void BGSWindow::stopAnimation() {
    if (!isPlaying) {return;}
    isPlaying = false;

    if (animationRunner) {
        delete animationRunner;
        animationRunner = nullptr;
    }

    updateAnimControls();
    showStatus("stopped");
}

void BGSWindow::nextFrame() {
    if (frameFiles.empty()) {return;}
    const char* framePath = frameFiles[currentFrame].c_str();

    if (setWallpaper(framePath) == B_OK) {
        refreshTracker();

        char statusMsg[256];
        snprintf(statusMsg, sizeof(statusMsg), "Frame %d/%zu", currentFrame + 1, frameFiles.size());

        showStatus(statusMsg);
    }

    currentFrame = (currentFrame + 1) % frameFiles.size();
}

void BGSWindow::updateAnimControls() {
    bool hasFrames = !frameFiles.empty();

    startButton -> SetEnabled(hasFrames && !isPlaying);
    stopButton -> SetEnabled(isPlaying);
}

status_t BGSWindow::setWallpaper(const char* imagePath) {
    BPath deskPath;
    if (find_directory(B_DESKTOP_DIRECTORY, &deskPath) != B_OK) {
        return B_ERROR;
    }


    BNode deskNode(deskPath.Path());
    if (deskNode.InitCheck() != B_OK) {
        return B_ERROR;
    }


    BMessage settings;

    int32 workspaceAmount = count_workspaces();
    int32 allWorkspaces = (1 << workspaceAmount) - 1;

    settings.AddInt32(B_BACKGROUND_WORKSPACES, allWorkspaces);
    settings.AddString(B_BACKGROUND_IMAGE, imagePath);
    settings.AddInt32(B_BACKGROUND_MODE, B_BACKGROUND_MODE_SCALED);
    settings.AddPoint(B_BACKGROUND_ORIGIN, BPoint(0, 0));
    settings.AddBool(B_BACKGROUND_ERASE_TEXT, true);

    ssize_t size = settings.FlattenedSize();

    char* buffer = new char[size];

    if (settings.Flatten(buffer, size) != B_OK) {
        delete[] buffer;
        return B_ERROR;
    }

    ssize_t written = deskNode.WriteAttr(B_BACKGROUND_INFO, B_MESSAGE_TYPE, 0, buffer, size);

    delete[] buffer;

    if (written != size) {
        return B_ERROR;
    }

    deskNode.Sync();

    return B_OK;
}

void BGSWindow::refreshTracker() {
    BMessenger tracker("application/x-vnd.Be-TRAK");
    tracker.SendMessage(B_RESTORE_BACKGROUND_IMAGE);
}

void BGSWindow::showStatus(const char* message) {
    statusLabel -> SetText(message);
}

bool BGSApp::isValidDir(const char* path) {
    if (strlen(path) == 0) return false;
    BEntry entry(path);

    return (entry.InitCheck() == B_OK && entry.Exists() && entry.IsDirectory());
}

bool BGSApp::loadAnimationFrames(const char* folderPath) {
    frameFiles.clear();
    currentFrame = 0;

    BDirectory dir(folderPath);
    if (dir.InitCheck() != B_OK) {
        return false;
    }

    std::vector<std::pair<int, std::string>> numberedFiles;

    BEntry entry;

    while (dir.GetNextEntry(&entry) == B_OK) {
        if (!entry.IsFile()) continue;

        char name[B_FILE_NAME_LENGTH];
        entry.GetName(name);

        std::string filename(name);
        size_t dp = filename.find_last_of('.');
        if (dp == std::string::npos) continue;

        std::string numberPart = filename.substr(0, dp);
        std::string extension =  filename.substr(dp + 1);

        if (extension != "png" && extension != "jpg" && extension != "jpeg" &&
            extension != "bmp" && extension != "gif" && extension != "tiff") { continue; }
            bool isNumeric = true;

        for (char c : numberPart) {
            if (!isdigit(c)) {
                isNumeric = false;
                break;
            }}

            if (isNumeric && !numberPart.empty()) {
                int frameNumber = std::stoi(numberPart);
                BPath fullPath;
                entry.GetPath(&fullPath);
                numberedFiles.push_back({frameNumber, std::string(fullPath.Path())});
            }
    }


    if (numberedFiles.empty()) {
        return false;
    }

    std::sort(numberedFiles.begin(), numberedFiles.end());

    for (const auto& pair : numberedFiles) {
        frameFiles.push_back(pair.second);
    }

    return true;

}

status_t BGSApp::setWallpaper(const char* imagePath) {
    BPath deskPath;
    if (find_directory(B_DESKTOP_DIRECTORY, &deskPath) != B_OK) {
        return B_ERROR;
    }


    BNode deskNode(deskPath.Path());
    if (deskNode.InitCheck() != B_OK) {
        return B_ERROR;
    }


    BMessage settings;

    int32 workspaceAmount = count_workspaces();
    int32 allWorkspaces = (1 << workspaceAmount) - 1;

    settings.AddInt32(B_BACKGROUND_WORKSPACES, allWorkspaces);
    settings.AddString(B_BACKGROUND_IMAGE, imagePath);
    settings.AddInt32(B_BACKGROUND_MODE, B_BACKGROUND_MODE_SCALED);
    settings.AddPoint(B_BACKGROUND_ORIGIN, BPoint(0, 0));
    settings.AddBool(B_BACKGROUND_ERASE_TEXT, true);

    ssize_t size = settings.FlattenedSize();

    char* buffer = new char[size];

    if (settings.Flatten(buffer, size) != B_OK) {
        delete[] buffer;
        return B_ERROR;
    }

    ssize_t written = deskNode.WriteAttr(B_BACKGROUND_INFO, B_MESSAGE_TYPE, 0, buffer, size);

    delete[] buffer;

    if (written != size) {
        return B_ERROR;
    }

    deskNode.Sync();

    return B_OK;
}

void BGSApp::refreshTracker() {
    BMessenger tracker("application/x-vnd.Be-TRAK");
    tracker.SendMessage(B_RESTORE_BACKGROUND_IMAGE);
}

void BGSApp::startCliAnimation(const char* path, int fps) {
    if (!isValidDir(path)) {
        printf("Invalid dir, maybe doesn't exist\n");
        return;
    }

    if (!loadAnimationFrames(path)) {
        printf("No valid numbered frames found (1.png, 2.png, ...)\n");
        return;
    }

    delay = 1000000 / fps;
    currentFrame = 0;

    printf("Playing animation!\n");

    while (true) {
        if (frameFiles.empty()) break;

        const char* framePath = frameFiles[currentFrame].c_str();

        if (setWallpaper(framePath) == B_OK) {
            refreshTracker();
            printf("Frame %d/%zu\n", currentFrame + 1, frameFiles.size());
        }

        currentFrame = (currentFrame + 1) % frameFiles.size();
        snooze(delay);
    }
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --animate <path> [fps]  Start animation from command line\n");
            printf("  --clear                 Clear background\n");
            printf("  --help, -h              Show this help\n");
            printf("  (no options)            Launch GUI\n");
            return 0;
        }

        if (strcmp(argv[1], "--animate") == 0) {
            if (argc < 3) {
                printf("Error: --animate requires a path\n");
                return 1;
            }

            int fps = 10;
            if (argc >= 4) {
                fps = atoi(argv[3]);
                if (fps < 1 || fps > 30) fps = 10;
            }

            BGSApp app(true);
            app.startCliAnimation(argv[2], fps);
            return 0;
        }

        if (strcmp(argv[1], "--clear") == 0) {
            BGSApp app(true);
            if (app.setWallpaper("") == B_OK) {
                app.refreshTracker();
                printf("cleared\n");
            } else {
                printf("Clearing failed?\n");
            }
            return 0;
        }
    }

    BGSApp app;
    app.Run();
    return 0;
}
