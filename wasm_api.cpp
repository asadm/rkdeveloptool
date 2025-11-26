#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#include <vector>
#include <string>
#include <cstdint>
#include <sys/stat.h>
#include <fstream>
#include <functional>
#include <vector>

#include "RKScan.h"
#include "RKComm.h"
#include "RKDevice.h"
#include "RKImage.h"
#include "RKLog.h"
#include "gpt.h"

extern CRKLog *g_pLogObject;
extern bool download_boot(STRUCT_RKDEVICE_DESC &dev, char *szLoader);

struct DeviceInfo {
	int devNo;
	uint16_t vid;
	uint16_t pid;
	uint32_t locationId;
	std::string type;
};

struct FlashInfoJs {
	std::string manufacturer;
	uint32_t flashSizeMiB;
	uint16_t blockSize;
	uint32_t pageSize;
	uint32_t sectorsPerBlock;
	uint32_t blockCount;
	uint8_t eccBits;
	uint8_t accessTime;
	uint8_t flashCs;
	uint16_t validSectorsPerBlock;
};

struct CapabilityInfo {
	bool directLba;
	bool first4mAccess;
	uint8_t rawFlag;
};

struct PartitionInfo {
	int index;
	uint32_t startLba;
	std::string name;
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

static bool with_device(const std::function<bool(CRKDevice *)> &fn)
{
	libusb_exit(nullptr);
	if (libusb_init(nullptr) < 0)
		return false;
	if (!g_vid_pid_set) {
		g_scan.SetVidPid();
		g_vid_pid_set = true;
	}

	int found = g_scan.Search(RKUSB_MASKROM | RKUSB_LOADER | RKUSB_MSC);
	if (found < 1)
		return false;

	STRUCT_RKDEVICE_DESC dev;
	int chosen = -1;
	// Prefer loader if present
	for (int i = 0; i < found; ++i) {
		if (g_scan.GetDevice(dev, i) && dev.emUsbType == RKUSB_LOADER) {
			chosen = i;
			break;
		}
	}
	if (chosen == -1) {
		chosen = 0;
		if (!g_scan.GetDevice(dev, chosen))
			return false;
	} else {
		g_scan.GetDevice(dev, chosen);
	}

	bool ok;
	CRKUsbComm *comm = new CRKUsbComm(dev, g_pLogObject, ok);
	if (!ok) {
		delete comm;
		return false;
	}
	CRKDevice *device = new CRKDevice(dev);
	if (!device) {
		delete comm;
		return false;
	}
	device->SetObject(nullptr, comm, g_pLogObject);

	bool res = fn(device);
	delete device; // owns comm
	return res;
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

static emscripten::val read_flash_id_js()
{
	std::vector<uint8_t> id(5, 0);
	bool ok = with_device([&](CRKDevice *dev) {
		int ret = dev->GetCommObjectPointer()->RKU_ReadFlashID(id.data());
		emscripten_log(EM_LOG_CONSOLE, "rkdeveloptool: readFlashId ret=%d", ret);
		return ret == ERR_SUCCESS;
	});
	if (!ok) {
		emscripten_log(EM_LOG_CONSOLE, "rkdeveloptool: readFlashId failed");
		return emscripten::val::null();
	}
	emscripten::val arr = emscripten::val::array();
	for (size_t i = 0; i < id.size(); ++i) arr.call<void>("push", id[i]);
	return arr;
}

static emscripten::val read_flash_info_js()
{
	FlashInfoJs info{};
	bool ok = with_device([&](CRKDevice *dev) {
		if (!dev->GetFlashInfo())
			return false;
		STRUCT_FLASH_INFO f = dev->GetFlashInfoStruct();
		info.manufacturer = f.szManufacturerName;
		info.flashSizeMiB = f.uiFlashSize;
		info.blockSize = f.usBlockSize;
		info.pageSize = f.uiPageSize;
		info.sectorsPerBlock = f.uiSectorPerBlock;
		info.blockCount = f.uiBlockNum;
		info.eccBits = f.bECCBits;
		info.accessTime = f.bAccessTime;
		info.flashCs = f.bFlashCS;
		info.validSectorsPerBlock = f.usValidSecPerBlock;
		return true;
	});
	if (!ok) {
		emscripten_log(EM_LOG_CONSOLE, "rkdeveloptool: readFlashInfo failed");
		return emscripten::val::null();
	}

	emscripten::val obj = emscripten::val::object();
	obj.set("manufacturer", info.manufacturer);
	obj.set("flashSizeMiB", info.flashSizeMiB);
	obj.set("blockSize", info.blockSize);
	obj.set("pageSize", info.pageSize);
	obj.set("sectorsPerBlock", info.sectorsPerBlock);
	obj.set("blockCount", info.blockCount);
	obj.set("eccBits", info.eccBits);
	obj.set("accessTime", info.accessTime);
	obj.set("flashCs", info.flashCs);
	obj.set("validSectorsPerBlock", info.validSectorsPerBlock);
	return obj;
}

static emscripten::val read_chip_info_js()
{
	std::vector<uint8_t> chip(CHIPINFO_LEN, 0);
	bool ok = with_device([&](CRKDevice *dev) {
		int ret = dev->GetCommObjectPointer()->RKU_ReadChipInfo(chip.data());
		emscripten_log(EM_LOG_CONSOLE, "rkdeveloptool: readChipInfo ret=%d", ret);
		return ret == ERR_SUCCESS;
	});
	if (!ok) {
		emscripten_log(EM_LOG_CONSOLE, "rkdeveloptool: readChipInfo failed");
		return emscripten::val::null();
	}
	emscripten::val arr = emscripten::val::array();
	for (size_t i = 0; i < chip.size(); ++i) arr.call<void>("push", chip[i]);
	return arr;
}

static emscripten::val read_capability_js()
{
	CapabilityInfo cap{};
	bool ok = with_device([&](CRKDevice *dev) {
		BYTE data[8] = {0};
		int ret = dev->GetCommObjectPointer()->RKU_ReadCapability(data);
		if (ret != ERR_SUCCESS)
			return false;
		emscripten_log(EM_LOG_CONSOLE, "rkdeveloptool: readCapability ret=%d raw=0x%x", ret, data[0]);
		cap.directLba = (data[0] & 0x1) != 0;
		cap.first4mAccess = (data[0] & 0x4) != 0;
		cap.rawFlag = data[0];
		return true;
	});
	if (!ok) return emscripten::val::null();

	emscripten::val obj = emscripten::val::object();
	obj.set("directLba", cap.directLba);
	obj.set("first4mAccess", cap.first4mAccess);
	obj.set("rawFlag", cap.rawFlag);
	return obj;
}

static bool test_device_js()
{
	bool ok = with_device([&](CRKDevice *dev) {
		return dev->TestDevice();
	});
	return ok;
}

static emscripten::val print_partitions_js()
{
	std::vector<PartitionInfo> parts;
	bool ok = with_device([&](CRKDevice *dev) {
		std::vector<uint8_t> master(34 * SECTOR_SIZE, 0);
		int ret = dev->GetCommObjectPointer()->RKU_ReadLBA(0, 34, master.data());
		if (ret != ERR_SUCCESS)
			return false;

		gpt_header *gptHead = (gpt_header *)(master.data() + SECTOR_SIZE);
		if (gptHead->signature != le64_to_cpu(GPT_HEADER_SIGNATURE))
			return false;

		gpt_entry *gptEntry = nullptr;
		uint8_t zerobuf[GPT_ENTRY_SIZE];
		memset(zerobuf, 0, GPT_ENTRY_SIZE);
		for (uint32_t i = 0; i < le32_to_cpu(gptHead->num_partition_entries); i++) {
			gptEntry = (gpt_entry *)(master.data() + 2 * SECTOR_SIZE + i * GPT_ENTRY_SIZE);
			if (memcmp(zerobuf, (uint8_t *)gptEntry, GPT_ENTRY_SIZE) == 0)
				break;
			PartitionInfo p{};
			p.index = static_cast<int>(i);
			p.startLba = (uint32_t)le64_to_cpu(gptEntry->starting_lba);
			char partName[36] = {0};
			uint32_t j = 0;
			while (gptEntry->partition_name[j] && j < sizeof(partName) - 1) {
				partName[j] = (char)gptEntry->partition_name[j];
				j++;
			}
			p.name = partName;
			parts.push_back(p);
		}
		return true;
	});
	if (!ok) return emscripten::val::null();
	emscripten::val arr = emscripten::val::array();
	for (size_t i = 0; i < parts.size(); ++i) {
		emscripten::val obj = emscripten::val::object();
		obj.set("index", parts[i].index);
		obj.set("startLba", parts[i].startLba);
		obj.set("name", parts[i].name);
		arr.call<void>("push", obj);
	}
	return arr;
}

static bool erase_flash_js()
{
	bool ok = with_device([&](CRKDevice *dev) {
		if (!dev->GetFlashInfo())
			return false;
		int ret = dev->EraseAllBlocks();
		return ret == 0;
	});
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
	emscripten::function("readFlashId", &read_flash_id_js);
	emscripten::function("readFlashInfo", &read_flash_info_js);
	emscripten::function("readChipInfo", &read_chip_info_js);
	emscripten::function("readCapability", &read_capability_js);
	emscripten::function("testDevice", &test_device_js);
	emscripten::function("printPartitions", &print_partitions_js);
	emscripten::function("eraseFlash", &erase_flash_js);
}
#endif
