#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#include <vector>
#include <string>
#include <cstdint>
#include <sys/stat.h>
#include <fstream>

#include "RKScan.h"
#include "RKComm.h"
#include "RKDevice.h"
#include "RKImage.h"
#include "RKLog.h"

extern CRKLog *g_pLogObject;
extern bool download_boot(STRUCT_RKDEVICE_DESC &dev, char *szLoader);

struct DeviceInfo {
	int devNo;
	uint16_t vid;
	uint16_t pid;
	uint32_t locationId;
	std::string type;
};

bool g_vid_pid_set = false;
CRKScan g_scan;

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

static bool perform_download_boot(const std::string &loader_path)
{
	mkdir("/tmp", 0777);

	/* Ensure fresh permission state */
	libusb_exit(nullptr);
	if (libusb_init(nullptr) < 0)
		return false;
	if (!g_vid_pid_set) {
		g_scan.SetVidPid();
		g_vid_pid_set = true;
	}

	int found = g_scan.Search(RKUSB_MASKROM | RKUSB_LOADER | RKUSB_MSC);
	emscripten_log(EM_LOG_CONSOLE, "rkdeveloptool: downloadBoot search found %d devices", found);
	if (found < 1)
		return false;

	STRUCT_RKDEVICE_DESC dev;
	if (!g_scan.GetDevice(dev, 0))
		return false;

	struct stat st;
	if (stat(loader_path.c_str(), &st) != 0) {
		emscripten_log(EM_LOG_CONSOLE, "rkdeveloptool: stat failed on %s (errno=%d)", loader_path.c_str(), errno);
		return false;
	} else {
		emscripten_log(EM_LOG_CONSOLE, "rkdeveloptool: loader %s size=%lld", loader_path.c_str(), (long long)st.st_size);
	}

	/* Use existing CLI helper; expects mutable char* */
	std::string path = loader_path;
	return download_boot(dev, path.data());
}

static bool download_boot_js(const std::string &loader_path)
{
	return perform_download_boot(loader_path);
}

static bool download_boot_buffer_js(const emscripten::val &dataVal)
{
	std::vector<uint8_t> data = emscripten::vecFromJSArray<uint8_t>(dataVal);
	std::string path = "/tmp/loader.bin";

	std::ofstream ofs(path.c_str(), std::ios::binary | std::ios::trunc);
	if (!ofs.good())
		return false;
	ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
	ofs.close();

	bool ok = perform_download_boot(path);
	unlink(path.c_str());
	return ok;
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
	emscripten::function("downloadBoot", &download_boot_js);
	emscripten::function("downloadBootBuffer", &download_boot_buffer_js);
}
#endif
