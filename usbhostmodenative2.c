#define _WIN32_DCOM
#include <windows.h>
#include <wbemidl.h>

const char *TITLE = "USB Host Mode - Native! 2";

const DWORD HOST_ROLE_VALUE = 1;
const DWORD FUNCTION_ROLE_VALUE = 2;

const unsigned short devicePIDs[] = {0x0597, 0x0304, 0x1A8B, 0x0497, 0x0897, 0x1497};

static BSTR language;

void fail(int n) {
	char msg[19];
	/*CoUninitialize();*/
	wsprintfA(msg, "Failed! %d", n);
	MessageBoxA(NULL, msg, TITLE, MB_OK | MB_ICONERROR);
	ExitProcess((unsigned)n);
}

unsigned long numOfQueryResultsWMI(IWbemServices *services, BSTR query)
{
	IEnumWbemClassObject *results = NULL;
	IWbemClassObject *queryResult = NULL;
	unsigned long resultsNum;
	HRESULT hr;

	hr = services->lpVtbl->ExecQuery(services, language, query, WBEM_FLAG_FORWARD_ONLY, NULL, &results); /* execute a query */
	if (FAILED(hr)) fail(4);
	hr = results->lpVtbl->Next(results, (unsigned long)WBEM_INFINITE, 1, &queryResult, &resultsNum); /* get the amount of entires found */
	if (FAILED(hr)) fail(5);
	results->lpVtbl->Release(results); /* release the interfaces */
	if (queryResult)
		queryResult->lpVtbl->Release(queryResult);

	return resultsNum;
}

void _main(void) {
	BSTR namespace;
	IWbemLocator *locator;
	IWbemServices *services;
	DWORD role, roleSize = sizeof(DWORD);
	const DWORD *newRole;
	unsigned short devicePID = 0;
	char regKeyPath[64], msg[74], pnpUtilArgs[32];
	HKEY regKey;
	size_t i;
	int ans;
	LSTATUS ls;
	HINSTANCE hi;
	HRESULT hr;

	language = SysAllocString(L"WQL");
	if (!language) fail(101);
	/* (https://stackoverflow.com/questions/1431103/how-to-obtain-data-from-wmi-using-a-c-application) */
	namespace = SysAllocString(L"ROOT\\CimV2");
	if (!namespace) fail(100);

	/* initialize COM */
	hr = CoInitializeEx(0, COINIT_MULTITHREADED);
	if (FAILED(hr)) fail(0);
	hr = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
	if (FAILED(hr)) fail(1);

	/* connect to WMI */
	hr = CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (void**)&locator);
	if (FAILED(hr)) fail(2);

	hr = locator->lpVtbl->ConnectServer(locator, namespace, NULL, NULL, NULL, 0, NULL, NULL, &services);
	if (FAILED(hr)) fail(3);

	SysFreeString(namespace);

	for (i = 0; i < sizeof(devicePIDs)/sizeof(*devicePIDs); ++i) {
		wchar_t queryTemp[67];
		BSTR query;
		unsigned n;
		wsprintfW(queryTemp, L"Select * from Win32_PnPEntity Where DeviceID = 'ACPI\\\\QCOM%04X\\\\0'", devicePIDs[i]);
		query = SysAllocString(queryTemp);
		if (!query) fail(102);
		n = numOfQueryResultsWMI(services, query);
		SysFreeString(query);
		if (n) {
			devicePID = devicePIDs[i];
			break;
		}
	}

	/* release WMI COM interfaces */
	services->lpVtbl->Release(services);
	locator->lpVtbl->Release(locator);
	/* unallocate all the other COM stuff */
	CoUninitialize();
	SysFreeString(language);	

	if (!devicePID) {
		MessageBoxA(NULL, "Your device is not supported.", TITLE, MB_OK | MB_ICONWARNING);
		ExitProcess(2);
	}

	wsprintfA(regKeyPath, "SYSTEM\\CurrentControlSet\\Enum\\ACPI\\QCOM%04X\\0\\Device Parameters", devicePID);
	ls = RegCreateKeyEx(HKEY_LOCAL_MACHINE, regKeyPath, 0, 0, 0, KEY_ALL_ACCESS, 0, &regKey, 0);
	if (ls != ERROR_SUCCESS) fail(6);

	ls = RegQueryValueExA(regKey, "RoleSwitchMode", NULL, NULL, (BYTE*)&role, &roleSize);
	if (ls == ERROR_FILE_NOT_FOUND) role = 0;
	else if (ls != ERROR_SUCCESS) fail(7);

	wsprintfA(msg, "USB Host Mode is currently %s. Do you wish to %s it?",
		role == FUNCTION_ROLE_VALUE ? "disabled" : role == HOST_ROLE_VALUE ? "enabled" : "in an unknown state",
		role == HOST_ROLE_VALUE ? "disable" : "enable");
	ans = MessageBoxA(NULL, msg, TITLE, MB_YESNO | MB_ICONQUESTION);
	if (ans == IDNO) {
		ExitProcess(0);
	}

	newRole = role == HOST_ROLE_VALUE ? &FUNCTION_ROLE_VALUE : &HOST_ROLE_VALUE;
	ls = RegSetValueEx(regKey, "RoleSwitchMode", 0, REG_DWORD, (BYTE*)newRole, sizeof(*newRole));
	RegCloseKey(regKey);
	if (ls != ERROR_SUCCESS) {
		MessageBoxA(NULL, "Failed to set role value!", TITLE, MB_OK | MB_ICONERROR);
		ExitProcess(1);
	}

	wsprintfA(pnpUtilArgs, "/restart-device ACPI\\QCOM%04X\\0", devicePID);
	hi = ShellExecuteA(NULL, "runas", "pnputil.exe", pnpUtilArgs, NULL, SW_HIDE);
	if ((int*)hi <= (int*)32) fail(8);

	MessageBoxA(NULL, "USB Host Mode successfully toggled!", TITLE, MB_OK | MB_ICONINFORMATION);

	ExitProcess(0);
}
