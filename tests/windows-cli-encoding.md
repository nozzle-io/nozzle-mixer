# Windows CLI Encoding

The application uses the CRT `wmain` entry point on Windows and converts each
UTF-16 argument to UTF-8 before the existing parser. Conversion uses
`WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS)` with checked sizing and
conversion calls, owned storage, and a null-terminated argv array. Invalid
UTF-16 is rejected, not replaced. No process locale or ANSI code page is changed.
Non-Windows entry behavior and core name limits are unchanged. MSVC uses its
normal console CRT startup; MinGW targets select Unicode startup with `-municode`.

CI launches `nozzle-mixer.exe` with Japanese and supplementary Unicode input/output names. A separate discovery probe compares the registered output name against independently supplied UTF-8 file bytes; the expected missing-input failure JSON must preserve both names. Unpaired UTF-16 surrogates must fail before parsing. This is registration/selection evidence, not Windows shader-composition or pixel evidence.

Run the command in the Windows Unicode CLI step of `.github/workflows/ci.yml`
against that job's built executable. The evidence file and the probe's expected
name file use Japanese and supplementary Unicode directory and file names.
Windows evidence streams use `std::filesystem::u8path`; the Windows-only probe
opens its path directly from wide CRT arguments. Non-Windows file opens are
unchanged. This does not establish interactive console font/code-page behavior.

Contract: [Microsoft WideCharToMultiByte documentation](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte).
