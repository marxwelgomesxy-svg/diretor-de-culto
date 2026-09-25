/*
 * Diretor de Culto - native OBS Studio plugin
 *
 * Compatibility target: OBS Studio 27.2.4
 * 100% C++ / Qt. No Lua, Python or external script is required.
 *
 * OBS 27.2.4 uses the legacy obs_frontend_add_dock(QDockWidget*)
 * API. The newer obs_frontend_add_dock_by_id()/remove_dock() APIs
 * were added much later and are intentionally not used here.
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
		QMetaObject::invokeMethod(g_dock, "refreshFromObs",
					  Qt::QueuedConnection);
	}
}

static void create_dock()
{
	if (g_dock_window)
		return;

	auto *mainWindow = static_cast<QMainWindow *>(
		obs_frontend_get_main_window());

	if (!mainWindow) {
		blog(LOG_WARNING,
		     "[Diretor de Culto] janela principal do OBS ainda não está disponível");
		return;
	}

	auto *dockWindow = new QDockWidget("Diretor de Culto", mainWindow);
	dockWindow->setObjectName("diretor-de-culto-dock");
	dockWindow->setAllowedAreas(Qt::LeftDockWidgetArea |
				    Qt::RightDockWidgetArea |
				    Qt::TopDockWidgetArea |
				    Qt::BottomDockWidgetArea);
	dockWindow->setFeatures(QDockWidget::DockWidgetMovable |
				QDockWidget::DockWidgetFloatable |
				QDockWidget::DockWidgetClosable);

	auto *content = new DiretorDock(dockWindow);
	dockWindow->setWidget(content);

	/*
	 * OBS 27.2.4: this is the native Frontend API for registering
	 * a QDockWidget in the Docks menu.
	 */
	obs_frontend_add_dock(static_cast<void *>(dockWindow));

	g_dock_window = dockWindow;
	g_dock = content;

	dockWindow->hide();
	content->refreshFromObs();

	blog(LOG_INFO,
	     "[Diretor de Culto] dock nativo criado para OBS 27.2.4");
}

bool obs_module_load(void)
{
	blog(LOG_INFO,
	     "[Diretor de Culto] carregando plugin nativo C++/Qt para OBS 27.2.4");

	obs_frontend_add_event_callback(frontend_event, nullptr);

	/*
	 * OBS must have its main window initialized before the QDockWidget
	 * is registered. Use the Qt event queue so creation happens after
	 * the frontend is ready.
	 */
	if (QApplication::instance()) {
		QMetaObject::invokeMethod(QApplication::instance(), create_dock,
					  Qt::QueuedConnection);
	}

	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[Diretor de Culto] descarregando plugin");

	obs_frontend_remove_event_callback(frontend_event, nullptr);

	/*
	 * In OBS 27.2.4 there is no obs_frontend_remove_dock().
	 * Destroying the QDockWidget removes the native Qt dock.
	 */
	if (g_dock_window) {
		g_dock_window->close();
		delete g_dock_window;
	}

	g_dock = nullptr;
	g_dock_window = nullptr;
}

const char *obs_module_description(void)
{
	return "Diretor de Culto - diretor assistivo nativo para "
	       "transmissões no OBS Studio";
}
