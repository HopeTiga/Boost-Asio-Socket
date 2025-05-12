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

	//开始解析;
	Json::Value returnJson;
	//解析Json的对象;
	Json::Reader reader;

	Json::Value sourceJson;

 	const std::wstring urls = stringToWstring(url);

	//初始化一个WinHTTP-session句柄，“IrregularPacking”为此句柄的名称

	HINTERNET hSession = WinHttpOpen(L"AmbitionTc Code", NULL, NULL, NULL, NULL);

	if (hSession == NULL) {

		std::cout << "Error:Open session failed: " << GetLastError() << std::endl;

		returnJson["error"] = ErrorCodes::Error_Json;

		return returnJson;
	}

	//通过上述句柄连接到服务器，需要指定服务器IP和端口号。若连接成功，返回的hConnect句柄不为NULL。
	HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", (INTERNET_PORT)8080, 0);
	//注意：这里使用的是IP地址，也可以使用域名。
	if (hConnect == NULL) {
		std::cout << "Error:Connect failed: " << GetLastError() << std::endl;
		returnJson["error"] = ErrorCodes::Error_Json;

		return returnJson;
	}

	std::wcout << urls << std::endl;

	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urls.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
	//其中参数2表示请求方式，此处为Post；参数3:给定Post的具体地址，如这里的具体地址为http://192.168.50.112/getServiceInfo
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

	//4-1. 向服务器发送post数据
	//(1) 指定发送的数据内容
	std::string data = json.toStyledString();

	const void* ss = (const char*)data.c_str();

	//(2) 发送请求
	BOOL response;

	response = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, const_cast<void*>(ss), data.length(), data.length(), 0);

	if (!response) {

		std::cout << "Error:SendRequest failed: " << GetLastError() << std::endl;

		returnJson["error"] = ErrorCodes::Error_Json;

		return returnJson;
	}
	else {
		//（3） 发送请求成功则准备接受服务器的response。注意：在使用 WinHttpQueryDataAvailable和WinHttpReadData前必须使用WinHttpReceiveResponse才能access服务器返回的数据
		response = WinHttpReceiveResponse(hRequest, NULL);
	}

	LPVOID lpHeaderBuffer = NULL;

	DWORD dwSize = 0;

	if (response)
	{
		//(1) 获取header的长度
		WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
			WINHTTP_HEADER_NAME_BY_INDEX, NULL,
			&dwSize, WINHTTP_NO_HEADER_INDEX);

		//(2) 根据header的长度为buffer申请内存空间
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
		{
			lpHeaderBuffer = new WCHAR[dwSize / sizeof(WCHAR)];

			//(3) 使用WinHttpQueryHeaders获取header信息
			response = WinHttpQueryHeaders(hRequest,
				WINHTTP_QUERY_RAW_HEADERS_CRLF,
				WINHTTP_HEADER_NAME_BY_INDEX,
				lpHeaderBuffer, &dwSize,
				WINHTTP_NO_HEADER_INDEX);
		}
	}

	//解析上述header信息会发现服务器返回数据的charset为uft-8。这意味着后面需要对获取到的raw data进行宽字符转换。一开始由于没有意识到需要进行转换所以得到的数据都是乱码。
	//出现乱码的原因是：HTTP在传输过程中是二值的，它并没有text或者是unicode的概念。HTTP使用7bit的ASCII码作为HTTP headers，但是内容是任意的二值数据，需要根据header中指定的编码方式来描述它（通常是Content-Type header）.
	//因此当你接收到原始的HTTP数据时，先将其保存到char[] buffer中，然后利用WinHttpQueryHearders()获取HTTP头，得到内容的Content-Type,这样你就知道数据到底是啥类型的了，是ASCII还是Unicode或者其他。
	//一旦你知道了具体的编码方式，你就可以通过MultiByteToWideChar()将其转换成合适编码的字符，存入wchar_t[]中。
	//关于乱码的解决方案请看4-4

	//4-3. 获取服务器返回数据
	LPSTR pszOutBuffer = NULL;
	DWORD dwDownloaded = 0;         //实际收取的字符数
	wchar_t* pwText = NULL;
	std::vector<char*> pszOutBuffers;
	std::string inputString;

	if (response)
	{
		do
		{
			//(1) 获取返回数据的大小（以字节为单位）
			dwSize = 0;
			if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
				std::cout << "Error：WinHttpQueryDataAvailable failed：" << GetLastError() << std::endl;
				break;
			}
			if (!dwSize)    break;  //数据大小为0                
			//			cout << "dwSize" << dwSize << endl;
						//(2) 根据返回数据的长度为buffer申请内存空间
			pszOutBuffer = new char[dwSize + 1];

			pszOutBuffers.push_back(pszOutBuffer);
			if (!pszOutBuffer) {
				std::cout << "Out of memory." << std::endl;
				break;
			}
			ZeroMemory(pszOutBuffer, dwSize + 1);       //将buffer置0
			//(3) 通过WinHttpReadData读取服务器的返回数据
			if (!WinHttpReadData(hRequest, pszOutBuffer, dwSize, &dwDownloaded)) {
				std::cout << "Error：WinHttpQueryDataAvailable failed：" << GetLastError() << std::endl;
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

		for (auto it = pszOutBuffers.begin(); it != pszOutBuffers.end(); it++)			//释放空间
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