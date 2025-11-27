# rkDevelopTooljs

`rkDevelopTool` ported to WASM via libusb's WebUSB backend; runs entirely in the browser. Tested to flash RV1103/RV1106 from empty flash to a fully booting system using only the browser.

All exported WASM APIs are async (Asyncify-instrumented). Always `await` calls before reading results.

## Working APIs
- `await Module.listDevicesJs()`: returns an array of `{ devNo, vid, pid, locationId, type }` for Maskrom/Loader/MSC devices.
- `await Module.downloadBootBuffer(uint8Array) -> bool`: sends a loader to a Maskrom device; accepts a `Uint8Array` of the loader.
- `await Module.readFlashInfo() -> { manufacturer, flashSizeMiB, blockSize, pageSize, blockCount, eccBits, accessTime, flashCs, validSectorsPerBlock } | null`: probes flash info (requires Loader/Maskrom).
- `await Module.readFlashId() -> Uint8Array | null`: reads the flash ID.
- `await Module.readChipInfo() -> Uint8Array | null`: reads chip info.
- `await Module.readCapability() -> { directLba, first4mAccess, rawFlag } | null`: reads device capabilities.
- `await Module.printPartitions() -> Array<{ index, startLba, name }>`: reads partition table (GPT) if available.
- `await Module.writeLba(beginSector: number, data: Uint8Array) -> bool`: writes data starting at LBA; uses chunked transfers internally.
- `await Module.eraseFlash() -> bool`: erases the entire flash (dangerous).
- `await Module.resetDevice(subcode: number) -> bool`: resets the device with a subcode (commonly `0`).
- `await Module.testDevice() -> bool`: basic connectivity test.

### Minimal usage example
```js
import createModule from './rkDevelopTool_Mac.js';

const Module = await createModule({ noInitialRun: true });
await navigator.usb.requestDevice({ filters: [{ vendorId: 0x2207 }, { vendorId: 0x071B }] });
const devs = await Module.listDevicesJs();
console.log('Devices:', Array.from(devs || []));

const loaderData = await fetch('/path/to/loader.bin').then(r => r.arrayBuffer()).then(buf => new Uint8Array(buf));
const okBoot = await Module.downloadBootBuffer(loaderData);
if (!okBoot) throw new Error('downloadBootBuffer failed');

const info = await Module.readFlashInfo();
console.log('Flash info:', info);
```

## TODO / unverified
- `Module.downloadBoot(pathString)`: file-path based loader download (expects a writable FS path); use `downloadBootBuffer` in the browser instead.
- `Module.listDevices()`: legacy C++ vector return; `listDevicesJs` is preferred for JS.
- Additional helpers (read LBA, partial erase, progress callbacks) could be layered on top of existing primitives.

## License

- WASM parts (wasm_api.cpp, web/) licensed under the Apache License, Version 2.0.
- rkDevelopTool originally licensed under GNU General Public License.
- libusb licensed under GNU LESSER GENERAL PUBLIC LICENSE Version 2.1.