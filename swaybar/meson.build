tray_files = have_tray ? [
	'tray/host.c',
	'tray/icon.c',
	'tray/item.c',
	'tray/tray.c',
	'tray/watcher.c'
] : []

libsystemd = dependency('libsystemd')
libpulse = dependency('libpulse')

swaybar_deps = [
	cairo,
	client_protos,
	gdk_pixbuf,
	jsonc,
	libpulse,
	libsystemd,
	math,
	pango,
	pangocairo,
	rt,
	wayland_client,
	wayland_cursor
]
if have_tray
	if systemd.found()
		swaybar_deps += systemd
	elif elogind.found()
		swaybar_deps += elogind
	endif
endif

executable(
	'swaybar', [
		'badges.c',
		'badges_audio.c',
		'badges_battery.c',
		'badges_datetime.c',
		'badges_dbus.c',
		'badges_kbd_layout.c',
		'badges_load.c',
		'badges_network.c',
		'badges_notifications.c',
		'bar.c',
		'config.c',
		'i3bar.c',
		'input.c',
		'ipc.c',
		'main.c',
		'render.c',
		'status_line.c',
		'system_info.c',
		tray_files
	],
	include_directories: [sway_inc],
	dependencies: swaybar_deps,
	link_with: [lib_sway_common, lib_sway_client],
	install: true
)
