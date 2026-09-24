/*
 * Diretor de Culto - native OBS Studio plugin
 * 100% C++ / Qt. No Lua, Python or external script is required.
 *
 * Phase alpha:
 * - Native OBS dock
 * - Reads the real OBS current scene
 * - Detects scene changes through OBS Frontend API
 * - Manual / Assistido / Automático modes
 * - Rule-based suggestion engine
 * - Real scene switching
 * - Studio Mode support
 *
 * Vision/audio intelligence is deliberately not faked in this alpha.
 * The architecture is ready for the native vision/audio engines in the next phase.
 */

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QApplication>
#include <QPointer>

#include "diretor-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "pt-BR")

static QPointer<DiretorDock> g_dock;

static void frontend_event(enum obs_frontend_event event, void *private_data)
{
    Q_UNUSED(private_data);

    if (!g_dock)
        return;

    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING ||
        event == OBS_FRONTEND_EVENT_SCENE_CHANGED ||
        event == OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED ||
        event == OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED ||
        event == OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED ||
        event == OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED) {
        QMetaObject::invokeMethod(g_dock, "refreshFromObs", Qt::QueuedConnection);
    }
}

bool obs_module_load(void)
{
    obs_log(LOG_INFO, "[Diretor de Culto] carregando plugin nativo C++/Qt");

    obs_frontend_add_event_callback(frontend_event, nullptr);

    if (QApplication::instance()) {
        auto createDock = []() {
            if (g_dock)
                return;

            g_dock = new DiretorDock();

            if (!obs_frontend_add_dock_by_id(
                    "diretor-de-culto-dock",
                    "Diretor de Culto",
                    static_cast<void *>(g_dock.data()))) {
                obs_log(LOG_WARNING, "[Diretor de Culto] não foi possível criar o dock");
                delete g_dock;
                g_dock = nullptr;
                return;
            }

            g_dock->refreshFromObs();
        };

        // OBS emits FINISHED_LOADING after its main UI is ready. A queued
        // invocation is also used here so creation happens on the Qt UI loop.
        QMetaObject::invokeMethod(QApplication::instance(), createDock, Qt::QueuedConnection);
    }

    return true;
}

void obs_module_unload(void)
{
    obs_log(LOG_INFO, "[Diretor de Culto] descarregando plugin");

    obs_frontend_remove_event_callback(frontend_event, nullptr);

    if (g_dock) {
        obs_frontend_remove_dock("diretor-de-culto-dock");
        g_dock = nullptr;
    }
}

const char *obs_module_description(void)
{
    return "Diretor de Culto - diretor assistivo nativo para transmissões no OBS Studio";
}
