#include "headless_application.h"

#include "Emu/RSX/Null/NullGSRender.h"
#include "Emu/Cell/Modules/cellMsgDialog.h"
#include "Emu/Cell/Modules/cellOskDialog.h"
#include "Emu/Cell/Modules/cellSaveData.h"
#include "Emu/Cell/Modules/sceNpTrophy.h"
#include "Emu/Io/Null/null_camera_handler.h"

#include <clocale>

// For now, a trivial constructor/destructor. May add command line usage later.
headless_application::headless_application(int& argc, char** argv) : QCoreApplication(argc, argv)
{
}

bool headless_application::Init()
{
	// Force init the emulator
	InitializeEmulator(m_active_user.empty() ? "00000001" : m_active_user, false);

	// Create callbacks from the emulator, which reference the handlers.
	InitializeCallbacks();

	// Create connects to propagate events throughout Gui.
	InitializeConnects();

	// As per Qt recommendations to avoid conflicts for POSIX functions
	std::setlocale(LC_NUMERIC, "C");

	return true;
}

void headless_application::InitializeConnects() const
{
	qRegisterMetaType<std::function<void()>>("std::function<void()>");
	connect(this, &headless_application::RequestCallAfter, this, &headless_application::HandleCallAfter);
}

/** RPCS3 emulator has functions it desires to call from the GUI at times. Initialize them in here. */
void headless_application::InitializeCallbacks()
{
	EmuCallbacks callbacks = CreateCallbacks();

	callbacks.try_to_quit = [this](bool force_quit, std::function<void()> on_exit) -> bool
	{
		if (force_quit)
		{
			if (on_exit)
			{
				on_exit();
			}

			quit();
			return true;
		}

		return false;
	};
	callbacks.call_after = [this](std::function<void()> func)
	{
		RequestCallAfter(std::move(func));
	};

	callbacks.init_gs_render = []()
	{
		switch (const video_renderer type = g_cfg.video.renderer)
		{
		case video_renderer::null:
		{
			g_fxo->init<rsx::thread, named_thread<NullGSRender>>();
			break;
		}
		case video_renderer::opengl:
#if defined(HAVE_VULKAN)
		case video_renderer::vulkan:
#endif
		{
			fmt::throw_exception("Headless mode can only be used with the %s video renderer. Current renderer: %s", video_renderer::null, type);
			[[fallthrough]];
		}
		default:
		{
			fmt::throw_exception("Invalid video renderer: %s", type);
		}
		}
	};

	callbacks.get_camera_handler = []() -> std::shared_ptr<camera_handler_base>
	{
		switch (g_cfg.io.camera.get())
		{
		case camera_handler::null:
		case camera_handler::fake:
		{
			return std::make_shared<null_camera_handler>();
		}
		case camera_handler::qt:
		{
			fmt::throw_exception("Headless mode can not be used with this camera handler. Current handler: %s", g_cfg.io.camera.get());
		}
		}
		return nullptr;
	};

	callbacks.get_gs_frame = []() -> std::unique_ptr<GSFrameBase>
	{
		if (g_cfg.video.renderer != video_renderer::null)
		{
			fmt::throw_exception("Headless mode can only be used with the %s video renderer. Current renderer: %s", video_renderer::null, g_cfg.video.renderer.get());
		}
		return std::unique_ptr<GSFrameBase>();
	};

	callbacks.get_msg_dialog                 = []() -> std::shared_ptr<MsgDialogBase> { return std::shared_ptr<MsgDialogBase>(); };
	callbacks.get_osk_dialog                 = []() -> std::shared_ptr<OskDialogBase> { return std::shared_ptr<OskDialogBase>(); };
	callbacks.get_save_dialog                = []() -> std::unique_ptr<SaveDialogBase> { return std::unique_ptr<SaveDialogBase>(); };
	callbacks.get_trophy_notification_dialog = []() -> std::unique_ptr<TrophyNotificationBase> { return std::unique_ptr<TrophyNotificationBase>(); };

	callbacks.on_run    = [](bool /*start_playtime*/) {};
	callbacks.on_pause  = []() {};
	callbacks.on_resume = []() {};
	callbacks.on_stop   = []() {};
	callbacks.on_ready  = []() {};

	callbacks.on_missing_fw = []() { return false; };

	callbacks.handle_taskbar_progress = [](s32, s32) {};

	callbacks.get_localized_string    = [](localized_string_id, const char*) -> std::string { return {}; };
	callbacks.get_localized_u32string = [](localized_string_id, const char*) -> std::u32string { return {}; };

	callbacks.play_sound = [](const std::string&){};

	Emu.SetCallbacks(std::move(callbacks));
}

/**
 * Using connects avoids timers being unable to be used in a non-qt thread. So, even if this looks stupid to just call func, it's succinct.
 */
void headless_application::HandleCallAfter(const std::function<void()>& func)
{
	func();
}
