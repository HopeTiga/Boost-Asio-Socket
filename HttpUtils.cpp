#include "HttpUtils.h"

std::wstring stringToWstring(const std::string& str) {
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
	std::wstring wstrTo(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
	return wstrTo;
}

HttpUtils::~HttpUtils() {

}

HttpUtils::HttpUtils() {


}

Json::Value HttpUtils::doPost(std::string url,Json::Value json) {

	//��ʼ����;
	Json::Value returnJson;
	//����Json�Ķ���;
	Json::Reader reader;

	Json::Value sourceJson;

 	const std::wstring urls = stringToWstring(url);

	//��ʼ��һ��WinHTTP-session�������IrregularPacking��Ϊ�˾��������

	HINTERNET hSession = WinHttpOpen(L"AmbitionTc Code", NULL, NULL, NULL, NULL);

	if (hSession == NULL) {

		std::cout << "Error:Open session failed: " << GetLastError() << std::endl;

		returnJson["error"] = ErrorCodes::Error_Json;

		return returnJson;
	}

	//ͨ������������ӵ�����������Ҫָ��������IP�Ͷ˿ںš������ӳɹ������ص�hConnect�����ΪNULL��
	HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", (INTERNET_PORT)8080, 0);
	//ע�⣺����ʹ�õ���IP��ַ��Ҳ����ʹ��������
	if (hConnect == NULL) {
		std::cout << "Error:Connect failed: " << GetLastError() << std::endl;
		returnJson["error"] = ErrorCodes::Error_Json;

		return returnJson;
	}

	std::wcout << urls << std::endl;

	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urls.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
	//���в���2��ʾ����ʽ���˴�ΪPost������3:����Post�ľ����ַ��������ľ����ַΪhttp://192.168.50.112/getServiceInfo
	if (hRequest == NULL) {
		std::cout << "Error:OpenRequest failed: " << GetLastError() << std::endl;

		returnJson["error"] = ErrorCodes::Error_Json;

		return returnJson;
	}

	LPCWSTR additionalHeaders = L"Content-Type:application/json;charset=utf-8";

	BOOL bHeaders = WinHttpAddRequestHeaders(hRequest, additionalHeaders, -1L, WINHTTP_ADDREQ_FLAG_ADD);

	if (!bHeaders) {

		std::cout << "Error:OpenRequest failed: " << GetLastError() << std::endl;

		returnJson["error"] = ErrorCodes::Error_Json;

		return returnJson;

	}

	//4-1. �����������post����
	//(1) ָ�����͵���������
	std::string data = json.toStyledString();

	const void* ss = (const char*)data.c_str();

	//(2) ��������
	BOOL response;

	response = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, const_cast<void*>(ss), data.length(), data.length(), 0);

	if (!response) {

		std::cout << "Error:SendRequest failed: " << GetLastError() << std::endl;

		returnJson["error"] = ErrorCodes::Error_Json;

		return returnJson;
	}
	else {
		//��3�� ��������ɹ���׼�����ܷ�������response��ע�⣺��ʹ�� WinHttpQueryDataAvailable��WinHttpReadDataǰ����ʹ��WinHttpReceiveResponse����access���������ص�����
		response = WinHttpReceiveResponse(hRequest, NULL);
	}

	LPVOID lpHeaderBuffer = NULL;

	DWORD dwSize = 0;

	if (response)
	{
		//(1) ��ȡheader�ĳ���
		WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
			WINHTTP_HEADER_NAME_BY_INDEX, NULL,
			&dwSize, WINHTTP_NO_HEADER_INDEX);

		//(2) ����header�ĳ���Ϊbuffer�����ڴ�ռ�
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
		{
			lpHeaderBuffer = new WCHAR[dwSize / sizeof(WCHAR)];

			//(3) ʹ��WinHttpQueryHeaders��ȡheader��Ϣ
			response = WinHttpQueryHeaders(hRequest,
				WINHTTP_QUERY_RAW_HEADERS_CRLF,
				WINHTTP_HEADER_NAME_BY_INDEX,
				lpHeaderBuffer, &dwSize,
				WINHTTP_NO_HEADER_INDEX);
		}
	}

	//��������header��Ϣ�ᷢ�ַ������������ݵ�charsetΪuft-8������ζ�ź�����Ҫ�Ի�ȡ����raw data���п��ַ�ת����һ��ʼ����û����ʶ����Ҫ����ת�����Եõ������ݶ������롣
	//���������ԭ���ǣ�HTTP�ڴ���������Ƕ�ֵ�ģ�����û��text������unicode�ĸ��HTTPʹ��7bit��ASCII����ΪHTTP headers����������������Ķ�ֵ���ݣ���Ҫ����header��ָ���ı��뷽ʽ����������ͨ����Content-Type header��.
	//��˵�����յ�ԭʼ��HTTP����ʱ���Ƚ��䱣�浽char[] buffer�У�Ȼ������WinHttpQueryHearders()��ȡHTTPͷ���õ����ݵ�Content-Type,�������֪�����ݵ�����ɶ���͵��ˣ���ASCII����Unicode����������
	//һ����֪���˾���ı��뷽ʽ����Ϳ���ͨ��MultiByteToWideChar()����ת���ɺ��ʱ�����ַ�������wchar_t[]�С�
	//��������Ľ�������뿴4-4

	//4-3. ��ȡ��������������
	LPSTR pszOutBuffer = NULL;
	DWORD dwDownloaded = 0;         //ʵ����ȡ���ַ���
	wchar_t* pwText = NULL;
	std::vector<char*> pszOutBuffers;
	std::string inputString;

	if (response)
	{
		do
		{
			//(1) ��ȡ�������ݵĴ�С�����ֽ�Ϊ��λ��
			dwSize = 0;
			if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
				std::cout << "Error��WinHttpQueryDataAvailable failed��" << GetLastError() << std::endl;
				break;
			}
			if (!dwSize)    break;  //���ݴ�СΪ0                
			//			cout << "dwSize" << dwSize << endl;
						//(2) ���ݷ������ݵĳ���Ϊbuffer�����ڴ�ռ�
			pszOutBuffer = new char[dwSize + 1];

			pszOutBuffers.push_back(pszOutBuffer);
			if (!pszOutBuffer) {
				std::cout << "Out of memory." << std::endl;
				break;
			}
			ZeroMemory(pszOutBuffer, dwSize + 1);       //��buffer��0
			//(3) ͨ��WinHttpReadData��ȡ�������ķ�������
			if (!WinHttpReadData(hRequest, pszOutBuffer, dwSize, &dwDownloaded)) {
				std::cout << "Error��WinHttpQueryDataAvailable failed��" << GetLastError() << std::endl;
			}
			if (!dwDownloaded)
				break;
		} while (dwSize > 0);

		std::string InputData = "";
		DWORD index = 0;
		for (DWORD i = 0; i < pszOutBuffers.size(); i++)
		{
			InputData.append(pszOutBuffers[i]);
		}

		inputString = InputData;

		for (auto it = pszOutBuffers.begin(); it != pszOutBuffers.end(); it++)			//�ͷſռ�
		{
			if (*it != NULL)
			{
				delete* it;
				*it = NULL;
			}
		}
	}

	if (hRequest) WinHttpCloseHandle(hRequest);

	if (hConnect) WinHttpCloseHandle(hConnect);

	if (hSession) WinHttpCloseHandle(hSession);

	reader.parse(inputString,returnJson);

	return returnJson;

}