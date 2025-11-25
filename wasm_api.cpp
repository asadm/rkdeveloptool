#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#include <vector>
#include <string>
#include <cstdint>

#include "RKScan.h"

struct DeviceInfo {
	int devNo;
	uint16_t vid;
	uint16_t pid;
	uint32_t locationId;
	std::string type;
};

namespace {
bool g_vid_pid_set = false;
CRKScan g_scan;
} // namespace

static std::vector<DeviceInfo> list_devices()
{
	/* Refresh WebUSB permission state by reinitializing libusb each call. */
	libusb_exit(nullptr);
	if (libusb_init(nullptr) < 0)
		return {};

	if (!g_vid_pid_set) {
		g_scan.SetVidPid();
		g_vid_pid_set = true;
	}

	std::vector<DeviceInfo> devices;

	int found = g_scan.Search(RKUSB_MASKROM | RKUSB_LOADER | RKUSB_MSC);
	emscripten_log(EM_LOG_CONSOLE, "rkdeveloptool: libusb search found %d devices", found);
	if (found <= 0)
		return devices;

	int list_size = g_scan.DEVICE_COUNTS;
	emscripten_log(EM_LOG_CONSOLE, "rkdeveloptool: DEVICE_COUNTS=%d", list_size);

	devices.reserve(list_size);
	for (int i = 0; i < list_size; ++i) {
		STRUCT_RKDEVICE_DESC desc;
		if (!g_scan.GetDevice(desc, i)) {
			emscripten_log(EM_LOG_CONSOLE, "rkdeveloptool: GetDevice(%d) failed", i);
			continue;
		}

		std::string type = "Unknown";
		if (desc.emUsbType == RKUSB_MASKROM)
			type = "Maskrom";
		else if (desc.emUsbType == RKUSB_LOADER)
			type = "Loader";
		else if (desc.emUsbType == RKUSB_MSC)
			type = "MSC";

		emscripten_log(EM_LOG_CONSOLE, "rkdeveloptool: dev %d vid=0x%x pid=0x%x usbtype=%s", i,
		              desc.usVid, desc.usPid, type.c_str());

		DeviceInfo info;
		info.devNo = i + 1;
		info.vid = desc.usVid;
		info.pid = desc.usPid;
		info.locationId = desc.uiLocationID;
		info.type = type;
		devices.push_back(info);
	}
	return devices;
}

static emscripten::val list_devices_js()
{
	return emscripten::val::array(list_devices());
}

EMSCRIPTEN_BINDINGS(rkdeveloptool_wasm_api) {
	emscripten::value_object<DeviceInfo>("DeviceInfo")
		.field("devNo", &DeviceInfo::devNo)
		.field("vid", &DeviceInfo::vid)
		.field("pid", &DeviceInfo::pid)
		.field("locationId", &DeviceInfo::locationId)
		.field("type", &DeviceInfo::type);

	emscripten::register_vector<DeviceInfo>("DeviceInfoVector");
	emscripten::function("listDevices", &list_devices);
	emscripten::function("listDevicesJs", &list_devices_js);
}
#endif
