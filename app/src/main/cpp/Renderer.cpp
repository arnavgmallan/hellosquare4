#include "Renderer.h"

#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <GLES3/gl3.h>
#include <memory>
#include <vector>
#include <android/imagedecoder.h>
#include <cstdlib>

#include "AndroidOut.h"
#include "Shader.h"
#include "Utility.h"
#include "TextureAsset.h"

//! executes glGetString and outputs the result to logcat
#define PRINT_GL_STRING(s) {aout << #s": "<< glGetString(s) << std::endl;}

/*!
 * @brief if glGetString returns a space separated list of elements, prints each one on a new line
 *
 * This works by creating an istringstream of the input c-style string. Then that is used to create
 * a vector -- each element of the vector is a new element in the input string. Finally a foreach
 * loop consumes this and outputs it to logcat using @a aout
 */
#define PRINT_GL_STRING_AS_LIST(s) { \
std::istringstream extensionStream((const char *) glGetString(s));\
std::vector<std::string> extensionList(\
        std::istream_iterator<std::string>{extensionStream},\
        std::istream_iterator<std::string>());\
aout << #s":\n";\
for (auto& extension: extensionList) {\
    aout << extension << "\n";\
}\
aout << std::endl;\
}

//! Color for cornflower blue. Can be sent directly to glClearColor
#define CORNFLOWER_BLUE 100 / 255.f, 149 / 255.f, 237 / 255.f, 1

// Vertex shader, you'd typically load this from assets
static const char *vertex = R"vertex(#version 300 es
in vec3 inPosition;
in vec2 inUV;

out vec2 fragUV;

uniform mat4 uProjection;
uniform mat4 uModel;

void main() {
    fragUV = inUV;
    gl_Position = uProjection * uModel * vec4(inPosition, 1.0);
}
)vertex";

// Fragment shader, you'd typically load this from assets
static const char *fragment = R"fragment(#version 300 es
precision mediump float;

in vec2 fragUV;

uniform sampler2D uTexture;
uniform vec4 uColor;
uniform bool uIsCircle;

out vec4 outColor;

void main() {
    if (uIsCircle) {
        float dist = distance(fragUV, vec2(0.5, 0.5));
        if (dist > 0.5) {
            discard;
        }
    }
    outColor = texture(uTexture, fragUV) * uColor;
}
)fragment";

/*!
 * Half the height of the projection matrix. This gives you a renderable area of height 4 ranging
 * from -2 to 2
 */
static constexpr float kProjectionHalfHeight = 2.f;

/*!
 * The near plane distance for the projection matrix. Since this is an orthographic projection
 * matrix, it's convenient to have negative values for sorting (and avoiding z-fighting at 0).
 */
static constexpr float kProjectionNearPlane = -1.f;

/*!
 * The far plane distance for the projection matrix. Since this is an orthographic porjection
 * matrix, it's convenient to have the far plane equidistant from 0 as the near plane.
 */
static constexpr float kProjectionFarPlane = 1.f;

static Renderer* gRenderer = nullptr;

Renderer::~Renderer() {
    gRenderer = nullptr;
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(display_, context_);
            context_ = EGL_NO_CONTEXT;
        }
        if (surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
            surface_ = EGL_NO_SURFACE;
        }
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
    }
}

void Renderer::render() {
    // Check to see if the surface has changed size. This is _necessary_ to do every frame when
    // using immersive mode as you'll get no other notification that your renderable area has
    // changed.
    updateRenderArea();
    updateGame();

    // When the renderable area changes, the projection matrix has to also be updated. This is true
    // even if you change from the sample orthographic projection matrix as your aspect ratio has
    // likely changed.
    if (shaderNeedsNewProjectionMatrix_) {
        // a placeholder projection matrix allocated on the stack. Column-major memory layout
        float projectionMatrix[16] = {0};

        // build an orthographic projection matrix for 2d rendering
        Utility::buildOrthographicMatrix(
                projectionMatrix,
                kProjectionHalfHeight,
                (float)width_ / (float)height_,
                kProjectionNearPlane,
                kProjectionFarPlane);

        // send the matrix to the shader
        // Note: the shader must be active for this to work. Since we only have one shader for this
        // demo, we can assume that it's active.
        shader_->setProjectionMatrix(projectionMatrix);

        // make sure the matrix isn't generated every frame
        shaderNeedsNewProjectionMatrix_ = false;
    }

    // clear the color buffer
    glClear(GL_COLOR_BUFFER_BIT);

    // Render all the models.
    if (models_.size() >= 2) {
        const auto &texturedSquare = models_[0];
        const auto &whiteSquare = models_[1];
        float modelMatrix[16];

        // 1. Stadium (Green Background with Robot Pattern)
        Utility::buildScaleMatrix(modelMatrix, 10.0f, 10.0f, 1.0f);
        shader_->setModelMatrix(modelMatrix);
        shader_->setColor(0.1f, 0.5f, 0.1f, 1.0f);
        shader_->setIsCircle(false);
        shader_->drawModel(texturedSquare);

        // 2. Stumps (Brown - Solid)
        shader_->setColor(0.6f, 0.3f, 0.0f, 1.0f);
        shader_->setIsCircle(false);
        for (int i = 0; i < 3; ++i) {
            float xPos = -0.15f + (float)i * 0.15f;
            Utility::buildScaleMatrix(modelMatrix, 0.05f, 0.3f, 1.0f); // Shorter: 0.3 instead of 0.4
            modelMatrix[12] = xPos;
            modelMatrix[13] = -1.85f; // Lowered slightly: -1.85 instead of -1.8
            shader_->setModelMatrix(modelMatrix);
            shader_->drawModel(whiteSquare);
        }

        // 3. Bat (Wood color - Solid)
        Utility::buildScaleMatrix(modelMatrix, batWidth_, batHeight_, 1.0f);
        modelMatrix[12] = batX_;
        modelMatrix[13] = batY_;
        shader_->setModelMatrix(modelMatrix);
        shader_->setColor(0.8f, 0.7f, 0.5f, 1.0f);
        shader_->setIsCircle(false);
        shader_->drawModel(whiteSquare);

        // 4. Ball (RED - Solid)
        Utility::buildScaleMatrix(modelMatrix, 0.15f, 0.15f, 1.0f); // Make it slightly bigger to see it better
        modelMatrix[12] = ballX_;
        modelMatrix[13] = ballY_;
        shader_->setModelMatrix(modelMatrix);
        if (isOut_) {
            shader_->setColor(0.5f, 0.5f, 0.5f, 1.0f); // Grey ball if out
        } else {
            shader_->setColor(1.0f, 0.0f, 0.0f, 1.0f); // PURE RED
        }
        shader_->setIsCircle(true); // IT'S A CIRCLE NOW!
        shader_->drawModel(whiteSquare);

        // 5. Helmet (Blue - Solid)
        Utility::buildScaleMatrix(modelMatrix, 0.2f, 0.2f, 1.0f);
        modelMatrix[12] = 1.0f;
        modelMatrix[13] = 1.5f;
        shader_->setModelMatrix(modelMatrix);
        shader_->setColor(0.0f, 0.0f, 0.8f, 1.0f);
        shader_->setIsCircle(true); // HELMET IS ALSO ROUND-ISH
        shader_->drawModel(whiteSquare);
    }

    // Present the rendered image. This is an implicit glFlush.
    auto swapResult = eglSwapBuffers(display_, surface_);
    assert(swapResult == EGL_TRUE);
}

void Renderer::updateGame() {
    if (!isBallActive_ || isOut_) return;

    ballX_ += ballVX_;
    ballY_ += ballVY_;

    // Collision with bat
    if (ballY_ < batY_ + 0.3f && ballY_ > batY_ - 0.3f &&
        ballX_ < batX_ + batWidth_ / 2.0f && ballX_ > batX_ - batWidth_ / 2.0f) {

        if (ballVY_ < 0) { // Only hit if ball is moving down
            if (hasTappedOnBat_) {
                int hit = (rand() % 2 == 0) ? 4 : 6;
                if (hit == 4) score4_++; else score6_++;
                totalScore_ += hit;

                aout << "*******************************" << std::endl;
                aout << " HIT FOR " << hit << " RUNS! " << std::endl;
                aout << " TOTAL SCORE: " << totalScore_ << std::endl;
                aout << " TABLE OF " << hit << ":" << std::endl;
                for (int i = 1; i <= 10; ++i) {
                    aout << " " << hit << " x " << i << " = " << (hit * i) << std::endl;
                }
                aout << "*******************************" << std::endl;

                callShowTable(hit, totalScore_, score4_, score6_);
                hasTappedOnBat_ = false; // Reset after a scoring hit
            }

            // Reverse ball and add some random horizontal velocity
            ballVY_ = 0.06f;
            ballVX_ = (float(rand() % 100) / 1000.0f) - 0.05f;
        }
    }

    // Collision with stumps (Out)
    if (ballY_ < -1.75f && ballY_ > -1.95f && ballX_ < 0.2f && ballX_ > -0.2f) {
        if (!isOut_) {
            isOut_ = true;
            aout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
            aout << " OUT! BALL HIT THE STUMPS! " << std::endl;
            aout << " FINAL SCORE: " << totalScore_ << std::endl;
            aout << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
            callShowOut(totalScore_);
        }
    }

    // Boundary checks
    if (ballX_ > 2.0f || ballX_ < -2.0f) ballVX_ = -ballVX_;

    // Reset ball if it goes off screen (and not out)
    if (ballY_ < -2.5f || ballY_ > 2.5f) {
        if (!isOut_) {
            ballX_ = 0.0f;
            ballY_ = 2.0f;
            ballVX_ = 0.0f;
            ballVY_ = -0.05f;
            hasTappedOnBat_ = false; // Reset tap when ball resets
        }
    }
}

void Renderer::initRenderer() {
    // Choose your render attributes
    constexpr EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE
    };

    // The default display is probably what you want on Android
    auto display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, nullptr, nullptr);

    // figure out how many configs there are
    EGLint numConfigs;
    eglChooseConfig(display, attribs, nullptr, 0, &numConfigs);

    // get the list of configurations
    std::unique_ptr<EGLConfig[]> supportedConfigs(new EGLConfig[numConfigs]);
    eglChooseConfig(display, attribs, supportedConfigs.get(), numConfigs, &numConfigs);

    // Find a config we like.
    // Could likely just grab the first if we don't care about anything else in the config.
    // Otherwise hook in your own heuristic
    auto config = *std::find_if(
            supportedConfigs.get(),
            supportedConfigs.get() + numConfigs,
            [&display](const EGLConfig &config) {
                EGLint red, green, blue, depth;
                if (eglGetConfigAttrib(display, config, EGL_RED_SIZE, &red)
                    && eglGetConfigAttrib(display, config, EGL_GREEN_SIZE, &green)
                    && eglGetConfigAttrib(display, config, EGL_BLUE_SIZE, &blue)
                    && eglGetConfigAttrib(display, config, EGL_DEPTH_SIZE, &depth)) {

                    aout << "Found config with " << red << ", " << green << ", " << blue << ", "
                         << depth << std::endl;
                    return red == 8 && green == 8 && blue == 8 && depth == 24;
                }
                return false;
            });

    aout << "Found " << numConfigs << " configs" << std::endl;
    aout << "Chose " << config << std::endl;

    // create the proper window surface
    EGLint format;
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);
    EGLSurface surface = eglCreateWindowSurface(display, config, app_->window, nullptr);

    // Create a GLES 3 context
    EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, nullptr, contextAttribs);

    // get some window metrics
    auto madeCurrent = eglMakeCurrent(display, surface, surface, context);
    assert(madeCurrent);

    display_ = display;
    surface_ = surface;
    context_ = context;

    // make width and height invalid so it gets updated the first frame in @a updateRenderArea()
    width_ = -1;
    height_ = -1;
    gRenderer = this;

    PRINT_GL_STRING(GL_VENDOR)
    PRINT_GL_STRING(GL_RENDERER)
    PRINT_GL_STRING(GL_VERSION)
    PRINT_GL_STRING_AS_LIST(GL_EXTENSIONS)

    shader_ = std::unique_ptr<Shader>(
            Shader::loadShader(vertex, fragment, "inPosition", "inUV",
                               "uProjection", "uModel", "uColor", "uIsCircle"));
    assert(shader_);

    // Initialize game state
    ballX_ = 0.0f; ballY_ = 2.0f;
    ballVX_ = 0.0f; ballVY_ = -0.03f; // Slower ball
    batX_ = 0.0f; batY_ = -1.5f;
    batWidth_ = 0.8f; batHeight_ = 0.2f; // Bigger bat
    score4_ = 0; score6_ = 0;
    totalScore_ = 0;
    isBallActive_ = true;
    isOut_ = false;
    hasTappedOnBat_ = false;

    // Note: there's only one shader in this demo, so I'll activate it here. For a more complex game
    // you'll want to track the active shader and activate/deactivate it as necessary
    shader_->activate();

    // setup any other gl related global states
    glClearColor(CORNFLOWER_BLUE);

    // enable alpha globally for now, you probably don't want to do this in a game
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // get some demo models into memory
    createModels();
}

void Renderer::updateRenderArea() {
    EGLint width;
    eglQuerySurface(display_, surface_, EGL_WIDTH, &width);

    EGLint height;
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &height);

    if (width != width_ || height != height_) {
        width_ = width;
        height_ = height;
        glViewport(0, 0, width, height);

        // make sure that we lazily recreate the projection matrix before we render
        shaderNeedsNewProjectionMatrix_ = true;
    }
}

/**
 * @brief Create any demo models we want for this demo.
 */
void Renderer::createModels() {
    /*
     * This is a unit square:
     * 0 --- 1
     * | \   |
     * |  \  |
     * |   \ |
     * 3 --- 2
     */
    std::vector<Vertex> vertices = {
            Vertex(Vector3{0.5f, 0.5f, 0}, Vector2{0, 0}), // 0
            Vertex(Vector3{-0.5f, 0.5f, 0}, Vector2{1, 0}), // 1
            Vertex(Vector3{-0.5f, -0.5f, 0}, Vector2{1, 1}), // 2
            Vertex(Vector3{0.5f, -0.5f, 0}, Vector2{0, 1}) // 3
    };
    std::vector<Index> indices = {
            0, 1, 2, 0, 2, 3
    };

    auto assetManager = app_->activity->assetManager;
    auto spAndroidRobotTexture = TextureAsset::loadAsset(assetManager, "android_robot.png");

    spWhiteTexture_ = TextureAsset::createSolidTexture(255, 255, 255, 255);

    // Create a single unit square model that we will reuse for everything.
    models_.emplace_back(vertices, indices, spAndroidRobotTexture);
    models_.emplace_back(vertices, indices, spWhiteTexture_);
}

void Renderer::callShowTable(int hit, int total, int count4, int count6) {
    JNIEnv *env;
    app_->activity->vm->AttachCurrentThread(&env, nullptr);
    jclass clazz = env->GetObjectClass(app_->activity->javaGameActivity);
    jmethodID methodID = env->GetMethodID(clazz, "showTable", "(IIII)V");
    env->CallVoidMethod(app_->activity->javaGameActivity, methodID, hit, total, count4, count6);
    app_->activity->vm->DetachCurrentThread();
}

void Renderer::callShowOut(int total) {
    JNIEnv *env;
    app_->activity->vm->AttachCurrentThread(&env, nullptr);
    jclass clazz = env->GetObjectClass(app_->activity->javaGameActivity);
    jmethodID methodID = env->GetMethodID(clazz, "showOut", "(I)V");
    env->CallVoidMethod(app_->activity->javaGameActivity, methodID, total);
    app_->activity->vm->DetachCurrentThread();
}

void Renderer::resetGame() {
    isOut_ = false;
    totalScore_ = 0;
    score4_ = 0;
    score6_ = 0;
    ballX_ = 0.0f;
    ballY_ = 2.0f;
    ballVX_ = 0.0f;
    ballVY_ = -0.05f;
    hasTappedOnBat_ = false;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_hellosquare4_MainActivity_nativeResetGame(JNIEnv *env, jobject thiz) {
    if (gRenderer) {
        gRenderer->resetGame();
    }
}

void Renderer::handleInput() {
    // handle all queued inputs
    auto *inputBuffer = android_app_swap_input_buffers(app_);
    if (!inputBuffer) {
        // no inputs yet.
        return;
    }

    // handle motion events (motionEventsCounts can be 0).
    for (auto i = 0; i < inputBuffer->motionEventsCount; i++) {
        auto &motionEvent = inputBuffer->motionEvents[i];
        auto action = motionEvent.action;

        // Find the pointer index, mask and bitshift to turn it into a readable value.
        auto pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        aout << "Pointer(s): ";

        // get the x and y position of this event if it is not ACTION_MOVE.
        auto &pointer = motionEvent.pointers[pointerIndex];
        auto x = GameActivityPointerAxes_getX(&pointer);
        auto y = GameActivityPointerAxes_getY(&pointer);

        // determine the action type and process the event accordingly.
        switch (action & AMOTION_EVENT_ACTION_MASK) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                if (isOut_) {
                    // Reset game on tap if out
                    isOut_ = false;
                    totalScore_ = 0;
                    score4_ = 0;
                    score6_ = 0;
                    ballX_ = 0.0f; ballY_ = 2.0f;
                    ballVX_ = 0.0f; ballVY_ = -0.05f;
                    hasTappedOnBat_ = false;
                }
                aout << "(" << pointer.id << ", " << x << ", " << y << ") "
                     << "Pointer Down";
                {
                    float aspect = (float)width_ / (float)height_;
                    float worldX = ((x / (float) width_) * 2.0f - 1.0f) * (kProjectionHalfHeight * aspect);
                    float worldY = ((1.0f - (y / (float) height_)) * 2.0f - 1.0f) * kProjectionHalfHeight;

                    // Check if the tap is on or near the bat (generous hitbox for kids)
                    if (worldX >= batX_ - batWidth_ && worldX <= batX_ + batWidth_ &&
                        worldY >= batY_ - batHeight_ * 2.0f && worldY <= batY_ + batHeight_ * 2.0f) {
                        hasTappedOnBat_ = true;
                        aout << " [TAP ON BAT!] ";
                    }

                    batX_ = worldX;
                }
                break;

            case AMOTION_EVENT_ACTION_CANCEL:
                // treat the CANCEL as an UP event: doing nothing in the app, except
                // removing the pointer from the cache if pointers are locally saved.
                // code pass through on purpose.
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_POINTER_UP:
                aout << "(" << pointer.id << ", " << x << ", " << y << ") "
                     << "Pointer Up";
                break;

            case AMOTION_EVENT_ACTION_MOVE:
                // There is no pointer index for ACTION_MOVE, only a snapshot of
                // all active pointers; app needs to cache previous active pointers
                // to figure out which ones are actually moved.
                for (auto index = 0; index < motionEvent.pointerCount; index++) {
                    pointer = motionEvent.pointers[index];
                    x = GameActivityPointerAxes_getX(&pointer);
                    y = GameActivityPointerAxes_getY(&pointer);
                    aout << "(" << pointer.id << ", " << x << ", " << y << ")";

                    if (index != (motionEvent.pointerCount - 1)) aout << ",";
                    aout << " ";

                    float aspect = (float)width_ / (float)height_;
                    float worldX = ((x / (float) width_) * 2.0f - 1.0f) * (kProjectionHalfHeight * aspect);
                    batX_ = worldX;
                }
                aout << "Pointer Move";
                break;
            default:
                aout << "Unknown MotionEvent Action: " << action;
        }
        aout << std::endl;
    }
    // clear the motion input count in this buffer for main thread to re-use.
    android_app_clear_motion_events(inputBuffer);

    // handle input key events.
    for (auto i = 0; i < inputBuffer->keyEventsCount; i++) {
        auto &keyEvent = inputBuffer->keyEvents[i];
        aout << "Key: " << keyEvent.keyCode <<" ";
        switch (keyEvent.action) {
            case AKEY_EVENT_ACTION_DOWN:
                aout << "Key Down";
                break;
            case AKEY_EVENT_ACTION_UP:
                aout << "Key Up";
                break;
            case AKEY_EVENT_ACTION_MULTIPLE:
                // Deprecated since Android API level 29.
                aout << "Multiple Key Actions";
                break;
            default:
                aout << "Unknown KeyEvent Action: " << keyEvent.action;
        }
        aout << std::endl;
    }
    // clear the key input count too.
    android_app_clear_key_events(inputBuffer);
}