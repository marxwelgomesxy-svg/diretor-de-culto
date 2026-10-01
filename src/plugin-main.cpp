/*
 * Diretor de Culto V7
 * OBS Studio 27.2.4 / Windows x64
 * Native C++/Qt. Gemini API is optional and requires the user's API key.
 */
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QMetaObject>
#include <QPointer>
#include "diretor-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "pt-BR")

static QPointer<DiretorDock> g_dock;
static QPointer<QDockWidget> g_dock_window;

static void frontend_event(enum obs_frontend_event event, void *)
{
    if (!g_dock)
        return;
    switch (event) {
    case OBS_FRONTEND_EVENT_FINISHED_LOADING:
    case OBS_FRONTEND_EVENT_SCENE_CHANGED:
    case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
    case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
    case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
    case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
    case OBS_FRONTEND_EVENT_STREAMING_STARTED:
    case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
    case OBS_FRONTEND_EVENT_STREAMING_STARTING:
    case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
        QMetaObject::invokeMethod(g_dock, "refreshFromObs", Qt::QueuedConnection);
        break;
    default:
        break;
    }
}

static void create_dock()
{
    if (g_dock_window)
        return;
    auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
    if (!mainWindow) {
        blog(LOG_WARNING, "[Diretor de Culto] janela principal do OBS ainda não está disponível");
        return;
    }

    auto *dockWindow = new QDockWidget(QStringLiteral("Diretor de Culto"), mainWindow);
    dockWindow->setObjectName(QStringLiteral("diretor-de-culto-dock"));
    dockWindow->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea |
                                Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    dockWindow->setFeatures(QDockWidget::DockWidgetMovable |
                            QDockWidget::DockWidgetFloatable |
                            QDockWidget::DockWidgetClosable);
    dockWindow->resize(420, 820);

    auto *content = new DiretorDock(dockWindow);
    dockWindow->setWidget(content);
    obs_frontend_add_dock(static_cast<void *>(dockWindow));

    g_dock_window = dockWindow;
    g_dock = content;
    dockWindow->hide();
    content->refreshFromObs();
    blog(LOG_INFO, "[Diretor de Culto] V5 carregada para OBS 27.2.4");
}

bool obs_module_load(void)
{
    blog(LOG_INFO, "[Diretor de Culto] carregando V7 nativa C++/Qt + IA opcional / regras predefinidas");
    obs_frontend_add_event_callback(frontend_event, nullptr);
    if (QApplication::instance())
        QMetaObject::invokeMethod(QApplication::instance(), create_dock, Qt::QueuedConnection);
    return true;
}

void obs_module_unload(void)
{
    obs_frontend_remove_event_callback(frontend_event, nullptr);
    if (g_dock_window) {
        g_dock_window->close();
        delete g_dock_window;
    }
    g_dock = nullptr;
    g_dock_window = nullptr;
}

const char *obs_module_description(void)
{
    return "Diretor de Culto V7 - direção assistida por visão e áudio com Gemini API";
}
