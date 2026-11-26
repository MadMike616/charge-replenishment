# Third-party notices

The binary is built using the official D2RLoader PluginSDK 0.4.0 headers (Plugin ABI 4). Its MIT notice is reproduced below. The SDK itself is not vendored in this repository.

MIT License

Copyright (c) 2026 D2RLoader contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

The D2R-Tools and RuffnecKk plugin collections were inspected read-only for native call-site references and collision checks. This plugin's implementation was written separately. No source from a collection without a verified license was incorporated. Native call contracts were checked against the game-code capture.

`data/vanilla-string-ids.json` contains only numeric ID reservation ranges observed in vanilla 3.4.95032 localization data. It contains no game localization text or assets.

First-run setup uses JSON for Modern C++ (nlohmann/json) 3.12.0, copyright (c) 2013-2025 Niels Lohmann, under the MIT license. Its source and full license are preserved in `third_party/nlohmann/`; the binary distribution includes `third_party/nlohmann/LICENSE.MIT`.

The shared first-run mutex, loose-mod path discovery, and atomic file-writing pattern were adapted from MadMike's MIT-licensed Character Resistance Display plugin. No other plugin source or behavior is modified by this plugin.
