#define _WIN32_DCOM

#include <stdio.h>
#include <windows.h>
#include <wbemidl.h>

#define BUFFER_SIZE 128 // couldn't be bothered to use dynamic memory allocation
#define HOST_ROLE_VALUE 1
#define FUNCTION_ROLE_VALUE 2
char *MSG_TITLE = "USB Host Mode Control";
char *ROLE_VALUE_NAME = "RoleSwitchMode";

char *addBackslashes(char *string) // do it at runtime because WHY NOT
{
	char *result;
	unsigned int amount = 0;
	size_t length = strlen(string);
	for (size_t i = 0; i < length; i++)
		if (string[i] == '\\') amount++;
	if (amount == 0) return string;
	result = malloc(length+amount+1);
	if (!result) return NULL;

	unsigned int offset = 0;
	for (size_t i = 0; i < length; i++) {
		result[i+offset] = string[i];
		if (string[i] == '\\') {
			result[i+offset+1] = '\\';
			offset++;
		}
	}
	result[length+offset] = '\0';
	return result;
}

void FAILEDROUTINE(int nBreakpoint)
{
	CoUninitialize();
	char errmsg[BUFFER_SIZE];
	sprintf_s(errmsg, BUFFER_SIZE, "FAILED! Breakpoint #%d", nBreakpoint);
	MessageBox(NULL, errmsg, MSG_TITLE, 0);
	exit(1);
}

void WMIInit(IWbemLocator** Locator, IWbemServices** Services) // (https://stackoverflow.com/questions/1431103/how-to-obtain-data-from-wmi-using-a-c-application)
{
	BSTR WMINamespace = SysAllocString(L"ROOT\\CimV2");
	HRESULT hr;

	// initialize COM
	hr = CoInitializeEx(0, COINIT_MULTITHREADED);
	if (FAILED(hr)) FAILEDROUTINE(0);
	hr = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
	if (FAILED(hr)) FAILEDROUTINE(1);

	// connect to WMI
	hr = CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (void**)Locator);
	if (FAILED(hr)) FAILEDROUTINE(2);

	hr = (*Locator)->lpVtbl->ConnectServer(*Locator, WMINamespace, NULL, NULL, NULL, 0, NULL, NULL, Services);
	if (FAILED(hr)) FAILEDROUTINE(3);

	SysFreeString(WMINamespace);
}

ULONG WMIQueryNumOfResults(IWbemServices* services, BSTR query)
{
	IEnumWbemClassObject* results = NULL;
	IWbemClassObject* queryResult = NULL;

	BSTR language = SysAllocString(L"WQL");

	ULONG numberOfResults;

	services->lpVtbl->ExecQuery(services, language, query, WBEM_FLAG_FORWARD_ONLY, NULL, &results); // execute a query
	results->lpVtbl->Next(results, WBEM_INFINITE, 1, &queryResult, &numberOfResults); // get the amount of entires found
	results->lpVtbl->Release(results); // release the interfaces (this shouldn't be NULL)
	if (queryResult)
		queryResult->lpVtbl->Release(queryResult);
	SysFreeString(language);

	return numberOfResults;
}

void SetRoleValue(HKEY hKey, char *currentState, char *newState, const DWORD newRoleValue, const char *pDeviceID)
{
	char Message[BUFFER_SIZE];
	sprintf_s(Message, BUFFER_SIZE, "USB Host Mode is currently %s. Do you wish to %s it?", currentState, newState); // build a message considering the current host mode state
	int Answer = MessageBox(0, Message, MSG_TITLE, MB_YESNO + MB_ICONQUESTION); // ask the user if they want to toggle host mode
	if (Answer == IDNO) // do nothing if the user selects 'no'
		return;

	LSTATUS lStatus = RegSetValueEx(hKey, ROLE_VALUE_NAME, 0, REG_DWORD, (LPBYTE)&newRoleValue, sizeof(newRoleValue)); // set the registry value
	if (lStatus) FAILEDROUTINE(6);

	char PnpUtilOperation[BUFFER_SIZE];
	sprintf_s(PnpUtilOperation, BUFFER_SIZE, "/restart-device %s", pDeviceID);
	HINSTANCE hInstance = ShellExecute(NULL, "runas", "pnputil.exe", PnpUtilOperation, NULL, SW_HIDE); // restart the usb controller (or whatever it is) by using pnputil
	if ((int*)hInstance < (int*)32) FAILEDROUTINE(7); // microsoft what the fuck is this

	sprintf_s(Message, BUFFER_SIZE, "USB Host Mode successfully %sd.", newState);
	MessageBox(0, Message, MSG_TITLE, MB_OK + MB_ICONINFORMATION);
}

int main(void)
{
	int iResult;

	char *deviceIDs[] = { "ACPI\\QCOM0597\\0", "ACPI\\QCOM0304\\0", "ACPI\\QCOM1A8B\\0", "ACPI\\QCOM0497\\0", "ACPI\\QCOM0897\\0" /*, "ACPI\\PNP0501\\1"*/ };

	// WMI stuff
	IWbemLocator* locator = NULL;
	IWbemServices* services = NULL;

	// regsitry stuff
	long lResult;
	HKEY hKey;

	WMIInit(&locator, &services);

	const char *pDeviceID = NULL;

	for (size_t i = 0; i < sizeof(deviceIDs) / sizeof(*deviceIDs); i++) { // size of all pointers in the array is divided by the size of one pointer
		char *deviceIDWQL = addBackslashes(deviceIDs[i]);
		if (!deviceIDWQL) FAILEDROUTINE(4);
		char queryStr[BUFFER_SIZE];
		sprintf_s(queryStr, BUFFER_SIZE, "Select * from Win32_PnPEntity Where DeviceID = '%s'", deviceIDWQL); // build a WMI query
		free(deviceIDWQL);
		wchar_t queryWStr[BUFFER_SIZE];
		iResult = MultiByteToWideChar(CP_ACP, 0, queryStr, sizeof(queryStr), queryWStr, sizeof(queryWStr));
		if (!iResult) FAILEDROUTINE(5);
		BSTR query = SysAllocString(queryWStr); // COM APIs use BSTR
		ULONG numberOfResults = WMIQueryNumOfResults(services, query); // get NumberOfResults for our current query
		SysFreeString(query);
		if (numberOfResults > 0) {
			pDeviceID = deviceIDs[i]; // assign the correct device ID's memory address to the pDeviceID pointer
			break; // break the loop if a device has been found
		}
	}

	if (!pDeviceID) {

		MessageBox(0, "Your device is not supported!", MSG_TITLE, MB_OK + MB_ICONERROR);
		return 1;
	}

	// release WMI COM interfaces
	services->lpVtbl->Release(services);
	locator->lpVtbl->Release(locator);
	// unallocate all the other COM stuff
	CoUninitialize();

	char RegKey[BUFFER_SIZE];
	sprintf_s(RegKey, BUFFER_SIZE, "SYSTEM\\CurrentControlSet\\Enum\\%s\\Device Parameters", pDeviceID); // build the registry path

	lResult = RegCreateKeyEx(HKEY_LOCAL_MACHINE, RegKey, 0, 0, 0, KEY_ALL_ACCESS, 0, &hKey, 0); // create and/or open the key to hKkey
	if (lResult != ERROR_SUCCESS) {
		MessageBox(0, "couldn't open the registry key", MSG_TITLE, MB_OK + MB_ICONERROR);
		return 1;
	}

	DWORD roleValue = 0;
	DWORD roleValueBufferSize = sizeof(roleValue); // it has to be a variable, the Query command writes back to it
	lResult = RegQueryValueEx(hKey, ROLE_VALUE_NAME, NULL, NULL, (LPBYTE)&roleValue, &roleValueBufferSize); // read the current role value

	switch (roleValue) {
		case HOST_ROLE_VALUE:
			SetRoleValue(hKey, "enabled", "disable", FUNCTION_ROLE_VALUE, pDeviceID);
			break;
		case FUNCTION_ROLE_VALUE:
			SetRoleValue(hKey, "disabled", "enable", HOST_ROLE_VALUE, pDeviceID);
			break;
		default:
			SetRoleValue(hKey, "in an unknown state", "enable", HOST_ROLE_VALUE, pDeviceID);
	}

	RegCloseKey(hKey);

	return 0;
}
