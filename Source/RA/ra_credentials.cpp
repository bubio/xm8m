#include "ra_credentials.h"
#include "platform_path.h"

#include "ra_file_util.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(__linux__) && !defined(__ANDROID__) && defined(XM8_RA_HAS_LIBSECRET)
#include <glib.h>
#include <libsecret/secret.h>
#elif defined(__ANDROID__)
#include "xm8jni.h"
#endif

namespace Xm8Ra {
namespace {

constexpr const char *kService = "net.retropc.pi.XM8.RetroAchievements";
constexpr size_t kMaxUsernameBytes = 256;
constexpr size_t kMaxTokenBytes = 4096;

bool SetError(std::string *error, const char *message)
{
	if (error != nullptr) {
		*error = message;
	}
	return false;
}

bool ValidateCredentials(const RaCredentials& credentials, std::string *error)
{
	if (credentials.username.empty()) {
		return SetError(error, "RA username is empty");
	}
	if (credentials.username.size() > kMaxUsernameBytes) {
		return SetError(error, "RA username is too long");
	}
	if (credentials.token.empty()) {
		return SetError(error, "RA token is empty");
	}
	if (credentials.token.size() > kMaxTokenBytes) {
		return SetError(error, "RA token is too long");
	}
	return true;
}

bool ValidateUsername(const std::string& username, std::string *error)
{
	if (username.empty()) {
		return SetError(error, "RA username is not stored");
	}
	if (username.size() > kMaxUsernameBytes) {
		return SetError(error, "RA username is too long");
	}
	return true;
}

bool IsTestMemoryBackendEnabled()
{
	const char *value = std::getenv("XM8_RA_CREDENTIALS_TEST_MEMORY");
	return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

std::mutex& TestCredentialsMutex()
{
	static std::mutex mutex;
	return mutex;
}

std::map<std::string, std::string>& TestCredentials()
{
	static std::map<std::string, std::string> credentials;
	return credentials;
}

std::string CredentialKey(const std::string& username)
{
	return std::string(kService) + ":" + username;
}

bool SaveTestToken(const std::string& username, const std::string& token,
	std::string *)
{
	std::lock_guard<std::mutex> lock(TestCredentialsMutex());
	TestCredentials()[CredentialKey(username)] = token;
	return true;
}

bool LoadTestToken(const std::string& username, std::string *token,
	std::string *error)
{
	std::lock_guard<std::mutex> lock(TestCredentialsMutex());
	const auto it = TestCredentials().find(CredentialKey(username));
	if (it == TestCredentials().end()) {
		return SetError(error, "RA token is not stored");
	}
	*token = it->second;
	return true;
}

bool DeleteTestToken(const std::string& username, std::string *)
{
	std::lock_guard<std::mutex> lock(TestCredentialsMutex());
	TestCredentials().erase(CredentialKey(username));
	return true;
}

bool WriteUsernameHint(const std::string& path, const std::string& username,
	std::string *error)
{
	std::vector<uint8_t> data(username.begin(), username.end());
	data.push_back('\n');
	return WriteRaFile(path, data.data(), data.size(), error) ||
		SetError(error, "failed to save RA username");
}

bool ReadUsernameHint(const std::string& path, std::string *username,
	std::string *error)
{
	if (username == nullptr) {
		return false;
	}
	username->clear();
	std::vector<uint8_t> data;
	if (!ReadRaFile(path, &data, kMaxUsernameBytes + 2, nullptr)) {
		return SetError(error, "RA username is not stored");
	}
	username->assign(data.begin(), data.end());
	const size_t newline = username->find('\n');
	if (newline != std::string::npos) username->resize(newline);
	if (!username->empty() && username->back() == '\r') {
		username->pop_back();
	}
	return ValidateUsername(*username, error);
}

#ifdef _WIN32
std::wstring Utf8ToWide(const std::string& text)
{
	if (text.empty()) {
		return std::wstring();
	}
	const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		text.c_str(), static_cast<int>(text.size()), nullptr, 0);
	if (size <= 0) {
		return std::wstring();
	}
	std::wstring wide(static_cast<size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(),
		static_cast<int>(text.size()), &wide[0], size);
	return wide;
}
#endif

#if defined(__APPLE__)
bool SetMacStatusError(std::string *error, const char *message,
	OSStatus status)
{
	if (error != nullptr) {
		char buffer[128];
		std::snprintf(buffer, sizeof(buffer), "%s (%d)", message,
			static_cast<int>(status));
		*error = buffer;
	}
	return false;
}

bool SetMacStageStatusError(std::string *error, const char *message,
	const char *stage, OSStatus status)
{
	if (error != nullptr) {
		char buffer[160];
		std::snprintf(buffer, sizeof(buffer), "%s at %s (%d)", message,
			stage, static_cast<int>(status));
		*error = buffer;
	}
	return false;
}

CFMutableDictionaryRef CreateMacBaseQuery(CFStringRef service, CFStringRef account)
{
	CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault,
		0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (query == nullptr) {
		return nullptr;
	}
	CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
	CFDictionarySetValue(query, kSecAttrService, service);
	CFDictionarySetValue(query, kSecAttrAccount, account);
	return query;
}

CFMutableDictionaryRef CreateMacQuery(CFStringRef service, CFStringRef account)
{
	CFMutableDictionaryRef query = CreateMacBaseQuery(service, account);
	if (query == nullptr) {
		return nullptr;
	}
	CFDictionarySetValue(query, kSecAttrSynchronizable,
		kSecAttrSynchronizableAny);
	return query;
}

CFMutableDictionaryRef CreateMacAddQuery(CFStringRef service, CFStringRef account,
	CFDataRef secret)
{
	CFMutableDictionaryRef query = CreateMacBaseQuery(service, account);
	if (query == nullptr) {
		return nullptr;
	}
	CFDictionarySetValue(query, kSecValueData, secret);
	CFDictionarySetValue(query, kSecAttrSynchronizable, kCFBooleanFalse);
	CFDictionarySetValue(query, kSecAttrAccessible,
		kSecAttrAccessibleAfterFirstUnlock);
	return query;
}

CFMutableDictionaryRef CreateMacUpdate(CFDataRef secret)
{
	CFMutableDictionaryRef update = CFDictionaryCreateMutable(kCFAllocatorDefault,
		0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (update == nullptr) {
		return nullptr;
	}
	CFDictionarySetValue(update, kSecValueData, secret);
	CFDictionarySetValue(update, kSecAttrSynchronizable, kCFBooleanFalse);
	CFDictionarySetValue(update, kSecAttrAccessible,
		kSecAttrAccessibleAfterFirstUnlock);
	return update;
}

CFMutableDictionaryRef CreateMacLoadQuery(CFStringRef service, CFStringRef account)
{
	CFMutableDictionaryRef query = CreateMacQuery(service, account);
	if (query == nullptr) {
		return nullptr;
	}
	CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
	CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
	return query;
}
#endif

bool SavePlatformToken(const std::string& username, const std::string& token,
	std::string *error)
{
	if (IsTestMemoryBackendEnabled()) {
		return SaveTestToken(username, token, error);
	}

#ifdef _WIN32
	const std::wstring target = Utf8ToWide(CredentialKey(username));
	const std::wstring user = Utf8ToWide(username);
	if (target.empty() || user.empty() || token.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
		return SetError(error, "RA credential cannot be encoded");
	}

	CREDENTIALW credential = {};
	credential.Type = CRED_TYPE_GENERIC;
	credential.TargetName = const_cast<LPWSTR>(target.c_str());
	credential.UserName = const_cast<LPWSTR>(user.c_str());
	credential.CredentialBlob =
		reinterpret_cast<LPBYTE>(const_cast<char *>(token.data()));
	credential.CredentialBlobSize = static_cast<DWORD>(token.size());
	credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
	if (CredWriteW(&credential, 0) == FALSE) {
		return SetError(error, "failed to save RA token");
	}
	return true;
#elif defined(__APPLE__)
	CFDataRef secret = CFDataCreate(kCFAllocatorDefault,
		reinterpret_cast<const UInt8 *>(token.data()),
		static_cast<CFIndex>(token.size()));
	CFStringRef service = CFStringCreateWithCString(kCFAllocatorDefault,
		kService, kCFStringEncodingUTF8);
	CFStringRef account = CFStringCreateWithCString(kCFAllocatorDefault,
		username.c_str(), kCFStringEncodingUTF8);
	if (secret == nullptr || service == nullptr || account == nullptr) {
		if (secret != nullptr) CFRelease(secret);
		if (service != nullptr) CFRelease(service);
		if (account != nullptr) CFRelease(account);
		return SetError(error, "failed to prepare RA token");
	}

	CFDictionaryRef add_query = CreateMacAddQuery(service, account, secret);
	OSStatus status = add_query == nullptr ? errSecParam :
		SecItemAdd(add_query, nullptr);
	const char *failed_stage = "add";
	if (add_query != nullptr) CFRelease(add_query);
	if (status == errSecDuplicateItem) {
		CFDictionaryRef query = CreateMacQuery(service, account);
		CFDictionaryRef update = CreateMacUpdate(secret);
		status = query == nullptr || update == nullptr ? errSecParam :
			SecItemUpdate(query, update);
		failed_stage = "update";
		if (update != nullptr) CFRelease(update);
		if (query != nullptr) CFRelease(query);
	}
	CFRelease(secret);
	CFRelease(service);
	CFRelease(account);
	if (status != errSecSuccess) {
		return SetMacStageStatusError(error, "failed to save RA token",
			failed_stage, status);
	}
	return true;
#elif defined(__linux__) && !defined(__ANDROID__) && defined(XM8_RA_HAS_LIBSECRET)
	static const SecretSchema schema = {
		"net.retropc.pi.XM8.RetroAchievements",
		SECRET_SCHEMA_NONE,
		{
			{ "service", SECRET_SCHEMA_ATTRIBUTE_STRING },
			{ "account", SECRET_SCHEMA_ATTRIBUTE_STRING },
			{ nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING }
		}
	};
	GError *gerror = nullptr;
	const gboolean ok = secret_password_store_sync(&schema,
		SECRET_COLLECTION_DEFAULT, "XM8 RetroAchievements", token.c_str(),
		nullptr, &gerror, "service", kService, "account", username.c_str(),
		nullptr);
	if (!ok) {
		if (gerror != nullptr) {
			g_error_free(gerror);
		}
		return SetError(error, "failed to save RA token");
	}
	return true;
#elif defined(__ANDROID__)
	return Android_RaSaveCredential(username.c_str(),
		reinterpret_cast<const unsigned char *>(token.data()), token.size()) != 0 ||
		SetError(error, "failed to save RA token");
#else
	return SetError(error, "secure RA credential storage is unavailable");
#endif
}

bool LoadPlatformToken(const std::string& username, std::string *token,
	std::string *error)
{
	if (token == nullptr) {
		return false;
	}
	token->clear();
	if (IsTestMemoryBackendEnabled()) {
		return LoadTestToken(username, token, error);
	}

#ifdef _WIN32
	const std::wstring target = Utf8ToWide(CredentialKey(username));
	if (target.empty()) {
		return SetError(error, "RA credential cannot be encoded");
	}
	PCREDENTIALW credential = nullptr;
	if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential) == FALSE) {
		return SetError(error, "RA token is not stored");
	}
	token->assign(reinterpret_cast<const char *>(credential->CredentialBlob),
		reinterpret_cast<const char *>(credential->CredentialBlob) +
		credential->CredentialBlobSize);
	CredFree(credential);
	return !token->empty() || SetError(error, "RA token is empty");
#elif defined(__APPLE__)
	CFStringRef service = CFStringCreateWithCString(kCFAllocatorDefault,
		kService, kCFStringEncodingUTF8);
	CFStringRef account = CFStringCreateWithCString(kCFAllocatorDefault,
		username.c_str(), kCFStringEncodingUTF8);
	if (service == nullptr || account == nullptr) {
		if (service != nullptr) CFRelease(service);
		if (account != nullptr) CFRelease(account);
		return SetError(error, "failed to prepare RA token lookup");
	}

	CFDictionaryRef query = CreateMacLoadQuery(service, account);
	CFTypeRef result = nullptr;
	const OSStatus status = query == nullptr ? errSecParam :
		SecItemCopyMatching(query, &result);
	if (query != nullptr) CFRelease(query);
	CFRelease(service);
	CFRelease(account);
	if (status != errSecSuccess || result == nullptr ||
		CFGetTypeID(result) != CFDataGetTypeID()) {
		if (result != nullptr) CFRelease(result);
		if (status == errSecItemNotFound) {
			return SetError(error, "RA token is not stored");
		}
		return SetMacStatusError(error, "failed to load RA token", status);
	}
	CFDataRef data = static_cast<CFDataRef>(result);
	const UInt8 *bytes = CFDataGetBytePtr(data);
	const CFIndex size = CFDataGetLength(data);
	token->assign(reinterpret_cast<const char *>(bytes),
		reinterpret_cast<const char *>(bytes) + size);
	CFRelease(result);
	return !token->empty() || SetError(error, "RA token is empty");
#elif defined(__linux__) && !defined(__ANDROID__) && defined(XM8_RA_HAS_LIBSECRET)
	static const SecretSchema schema = {
		"net.retropc.pi.XM8.RetroAchievements",
		SECRET_SCHEMA_NONE,
		{
			{ "service", SECRET_SCHEMA_ATTRIBUTE_STRING },
			{ "account", SECRET_SCHEMA_ATTRIBUTE_STRING },
			{ nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING }
		}
	};
	GError *gerror = nullptr;
	gchar *secret = secret_password_lookup_sync(&schema, nullptr, &gerror,
		"service", kService, "account", username.c_str(), nullptr);
	if (gerror != nullptr) {
		g_error_free(gerror);
	}
	if (secret == nullptr) {
		return SetError(error, "RA token is not stored");
	}
	token->assign(secret);
	secret_password_free(secret);
	return !token->empty() || SetError(error, "RA token is empty");
#elif defined(__ANDROID__)
	unsigned char *stored = nullptr;
	size_t stored_size = 0;
	if (Android_RaLoadCredential(username.c_str(), &stored, &stored_size) == 0) {
		return SetError(error, "RA token is not stored");
	}
	token->assign(reinterpret_cast<const char *>(stored), stored_size);
	volatile unsigned char *clear = stored;
	for (size_t index = 0; index < stored_size; ++index) clear[index] = 0;
	Android_RaFreeCredential(stored);
	return !token->empty() || SetError(error, "RA token is empty");
#else
	return SetError(error, "secure RA credential storage is unavailable");
#endif
}

bool DeletePlatformToken(const std::string& username, std::string *error)
{
	if (username.empty()) {
		return true;
	}
	if (IsTestMemoryBackendEnabled()) {
		return DeleteTestToken(username, error);
	}

#ifdef _WIN32
	const std::wstring target = Utf8ToWide(CredentialKey(username));
	if (target.empty()) {
		return SetError(error, "RA credential cannot be encoded");
	}
	if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) == FALSE &&
		GetLastError() != ERROR_NOT_FOUND) {
		return SetError(error, "failed to delete RA token");
	}
	return true;
#elif defined(__APPLE__)
	CFStringRef service = CFStringCreateWithCString(kCFAllocatorDefault,
		kService, kCFStringEncodingUTF8);
	CFStringRef account = CFStringCreateWithCString(kCFAllocatorDefault,
		username.c_str(), kCFStringEncodingUTF8);
	if (service == nullptr || account == nullptr) {
		if (service != nullptr) CFRelease(service);
		if (account != nullptr) CFRelease(account);
		return SetError(error, "failed to prepare RA token deletion");
	}
	CFDictionaryRef query = CreateMacQuery(service, account);
	const OSStatus status = query == nullptr ? errSecParam :
		SecItemDelete(query);
	if (query != nullptr) CFRelease(query);
	CFRelease(service);
	CFRelease(account);
	if (status != errSecSuccess && status != errSecItemNotFound) {
		return SetMacStatusError(error, "failed to delete RA token", status);
	}
	return true;
#elif defined(__linux__) && !defined(__ANDROID__) && defined(XM8_RA_HAS_LIBSECRET)
	static const SecretSchema schema = {
		"net.retropc.pi.XM8.RetroAchievements",
		SECRET_SCHEMA_NONE,
		{
			{ "service", SECRET_SCHEMA_ATTRIBUTE_STRING },
			{ "account", SECRET_SCHEMA_ATTRIBUTE_STRING },
			{ nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING }
		}
	};
	GError *gerror = nullptr;
	secret_password_clear_sync(&schema, nullptr, &gerror, "service", kService,
		"account", username.c_str(), nullptr);
	if (gerror != nullptr) {
		g_error_free(gerror);
		return SetError(error, "failed to delete RA token");
	}
	return true;
#elif defined(__ANDROID__)
	return Android_RaDeleteCredential(username.c_str()) != 0 ||
		SetError(error, "failed to delete RA token");
#else
	return true;
#endif
}

} // namespace

RaPlatformCredentialsStore::RaPlatformCredentialsStore(const std::string& ra_root)
	: ra_root_(ra_root)
{
}

std::string RaPlatformCredentialsStore::UsernameHintPath() const
{
	return PlatformPath::JoinPortable(ra_root_, "credentials_user.txt");
}

bool RaPlatformCredentialsStore::Save(const RaCredentials& credentials,
	std::string *error)
{
	if (!ValidateCredentials(credentials, error)) {
		return false;
	}
	if (!SavePlatformToken(credentials.username, credentials.token, error)) {
		return false;
	}
	if (!WriteUsernameHint(UsernameHintPath(), credentials.username, error)) {
		std::string ignored_error;
		DeletePlatformToken(credentials.username, &ignored_error);
		return false;
	}
	return true;
}

bool RaPlatformCredentialsStore::Load(RaCredentials *credentials,
	std::string *error) const
{
	if (credentials == nullptr) {
		return false;
	}
	credentials->username.clear();
	credentials->token.clear();

	std::string username;
	if (!ReadUsernameHint(UsernameHintPath(), &username, error)) {
		return false;
	}
	std::string token;
	if (!LoadPlatformToken(username, &token, error)) {
		return false;
	}
	credentials->username = username;
	credentials->token = token;
	return true;
}

bool RaPlatformCredentialsStore::Delete(std::string *error)
{
	std::string username;
	std::string ignored_error;
	ReadUsernameHint(UsernameHintPath(), &username, &ignored_error);
	if (!DeletePlatformToken(username, error)) {
		return false;
	}
	if (!RemoveRaFile(UsernameHintPath(), nullptr)) {
		return SetError(error, "failed to delete RA username");
	}
	return true;
}

void RaPlatformCredentialsStore::ClearSecret(RaCredentials *credentials) const
{
	if (credentials == nullptr) {
		return;
	}
	volatile char *token = credentials->token.empty() ? nullptr :
		&credentials->token[0];
	for (size_t i = 0; i < credentials->token.size(); i++) {
		token[i] = 0;
	}
	credentials->token.clear();
}

std::unique_ptr<RaCredentialsStore> CreatePlatformRaCredentialsStore(
	const std::string& ra_root)
{
	return std::unique_ptr<RaCredentialsStore>(
		new RaPlatformCredentialsStore(ra_root));
}

} // namespace Xm8Ra
