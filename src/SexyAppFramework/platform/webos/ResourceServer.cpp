#include "ResourceServer.h"
#include "BitmapFont.h"
#include "IndexHtml.h"
#include "graphics/GLPlatform.h"

#include <SDL.h>

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <random>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <thread>
#include <atomic>
#include <mutex>
#include <csignal>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

namespace Sexy
{

std::string GetWebOSAppDir();

bool WebOSHasGameResources()
{
	std::string appDir = GetWebOSAppDir();
	std::filesystem::path pakPath = std::filesystem::path(appDir) / "main.pak";
	std::filesystem::path propPath = std::filesystem::path(appDir) / "properties" / "resources.xml";

	std::error_code ec;
	if (!std::filesystem::is_regular_file(pakPath, ec) || std::filesystem::file_size(pakPath, ec) < 1024 * 1024)
		return false;

	if (!std::filesystem::is_regular_file(propPath, ec))
		return false;

	return true;
}

static std::string GetLocalIPAddress()
{
	struct ifaddrs* ifaddr = nullptr;
	if (getifaddrs(&ifaddr) == -1)
		return "";

	std::string bestIP = "";
	for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
	{
		if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET)
			continue;

		if ((ifa->ifa_flags & IFF_LOOPBACK) || !(ifa->ifa_flags & IFF_UP))
			continue;

		char host[NI_MAXHOST];
		if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
		                host, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST) == 0)
		{
			std::string ip(host);
			if (ip != "127.0.0.1" && !ip.empty())
			{
				std::string ifName(ifa->ifa_name ? ifa->ifa_name : "");
				if (ifName.find("wlan") != std::string::npos || ifName.find("eth") != std::string::npos || bestIP.empty())
				{
					bestIP = ip;
				}
			}
		}
	}

	freeifaddrs(ifaddr);
	return bestIP;
}

static bool IsSafePort(int port)
{
	if (port == 5060 || port == 5061) return false;
	if (port >= 6000 && port <= 6063) return false; // X11
	if (port >= 6665 && port <= 6669) return false; // IRC
	if (port == 6697 || port == 10080) return false;
	return true;
}

static int StartHttpServer(int& outPort)
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<int> dis(5000, 65000);

	std::vector<int> candidates = { 8372, 8080, 8888 };
	for (int port : candidates)
	{
		int fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) continue;

		int opt = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
		setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

		struct sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons(port);

		if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0 &&
		    listen(fd, 64) == 0)
		{
			outPort = port;
			return fd;
		}
		close(fd);
	}

	for (int attempt = 0; attempt < 100; ++attempt)
	{
		int port = dis(gen);
		if (!IsSafePort(port)) continue;

		int fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) continue;

		int opt = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
		setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

		struct sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons(port);

		if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0 &&
		    listen(fd, 64) == 0)
		{
			outPort = port;
			return fd;
		}
		close(fd);
	}

	return -1;
}

static std::string UrlDecode(const std::string& str)
{
	std::string result;
	result.reserve(str.size());
	for (size_t i = 0; i < str.size(); ++i)
	{
		if (str[i] == '%' && i + 2 < str.size())
		{
			int value = 0;
			std::istringstream is(str.substr(i + 1, 2));
			if (is >> std::hex >> value)
			{
				result += static_cast<char>(value);
				i += 2;
			}
			else
			{
				result += str[i];
			}
		}
		else if (str[i] == '+')
		{
			result += ' ';
		}
		else
		{
			result += str[i];
		}
	}
	return result;
}

static bool SendAll(int clientFd, const char* data, size_t len)
{
	size_t sent = 0;
	while (sent < len)
	{
		struct pollfd pfd{};
		pfd.fd = clientFd;
		pfd.events = POLLOUT;
		int pr = poll(&pfd, 1, 5000);
		if (pr <= 0) return false;

		ssize_t n = send(clientFd, data + sent, len - sent, MSG_NOSIGNAL);
		if (n < 0)
		{
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			return false;
		}
		if (n == 0) return false;
		sent += n;
	}
	return true;
}

static void SendResponse(int clientFd, int status, const std::string& statusText,
                         const std::string& contentType, const std::string& body)
{
	std::string resp = "HTTP/1.1 " + std::to_string(status) + " " + statusText + "\r\n";
	resp += "Content-Type: " + contentType + "\r\n";
	resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
	resp += "Access-Control-Allow-Origin: *\r\n";
	resp += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
	resp += "Access-Control-Allow-Headers: Content-Type, *\r\n";
	resp += "Connection: close\r\n\r\n";
	resp += body;

	SendAll(clientFd, resp.data(), resp.size());
}

static void CloseClient(int clientFd)
{
	shutdown(clientFd, SHUT_WR);
	char dummy[512];
	struct pollfd pfd{};
	pfd.fd = clientFd;
	pfd.events = POLLIN;
	while (poll(&pfd, 1, 100) > 0)
	{
		if (recv(clientFd, dummy, sizeof(dummy), 0) <= 0)
			break;
	}
	close(clientFd);
}

static std::atomic<bool> gServerRunning{false};
static std::atomic<bool> gLaunchRequested{false};
static std::mutex gStatusMutex;
static std::string gCurrentStatus = "Waiting for upload...";

static void SetStatus(const std::string& msg)
{
	std::lock_guard<std::mutex> lock(gStatusMutex);
	gCurrentStatus = msg;
}

static std::string GetStatus()
{
	std::lock_guard<std::mutex> lock(gStatusMutex);
	return gCurrentStatus;
}

static void HandleClient(int clientFd, const std::string& appDir)
{
	struct timeval tv{};
	tv.tv_sec = 10;
	tv.tv_usec = 0;
	setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(clientFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	int nodelay = 1;
	setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

	struct linger sl{};
	sl.l_onoff = 1;
	sl.l_linger = 5;
	setsockopt(clientFd, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl));

	std::vector<char> headerBuf(16384);
	ssize_t totalRead = 0;
	size_t headerEnd = std::string::npos;
	size_t headerSepLen = 4;

	while (totalRead < static_cast<ssize_t>(headerBuf.size()) - 1)
	{
		struct pollfd pfd{};
		pfd.fd = clientFd;
		pfd.events = POLLIN;
		int pr = poll(&pfd, 1, 3000);
		if (pr <= 0) break;

		ssize_t n = recv(clientFd, headerBuf.data() + totalRead, headerBuf.size() - 1 - totalRead, 0);
		if (n <= 0) break;
		totalRead += n;
		headerBuf[totalRead] = '\0';

		char* sep = strstr(headerBuf.data(), "\r\n\r\n");
		if (sep)
		{
			headerEnd = sep - headerBuf.data();
			headerSepLen = 4;
			break;
		}
		sep = strstr(headerBuf.data(), "\n\n");
		if (sep)
		{
			headerEnd = sep - headerBuf.data();
			headerSepLen = 2;
			break;
		}
	}

	if (totalRead <= 0 || headerEnd == std::string::npos)
	{
		if (totalRead >= 3 && static_cast<unsigned char>(headerBuf[0]) == 0x16 &&
		    static_cast<unsigned char>(headerBuf[1]) == 0x03)
		{
			std::string errMsg =
				"HTTP/1.1 400 Bad Request\r\n"
				"Content-Type: text/plain\r\n"
				"Connection: close\r\n\r\n"
				"This server uses plain HTTP. Please use http:// (not https://) in your address bar.\r\n";
			SendAll(clientFd, errMsg.data(), errMsg.size());
		}
		CloseClient(clientFd);
		return;
	}

	std::string headerStr(headerBuf.data(), headerEnd);
	size_t lineEnd = headerStr.find("\r\n");
	if (lineEnd == std::string::npos) lineEnd = headerStr.find('\n');

	std::string reqLine = (lineEnd != std::string::npos) ? headerStr.substr(0, lineEnd) : headerStr;
	std::istringstream reqStream(reqLine);
	std::string method, uri, version;
	reqStream >> method >> uri >> version;

	if (method == "OPTIONS")
	{
		SendResponse(clientFd, 204, "No Content", "text/plain", "");
		CloseClient(clientFd);
		return;
	}

	if (method == "GET")
	{
		if (uri == "/" || uri == "/index.html" || uri.rfind("/?", 0) == 0)
		{
			SendResponse(clientFd, 200, "OK", "text/html; charset=utf-8", GetEmbeddedIndexHtml());
		}
		else if (uri == "/api/status")
		{
			bool hasPak = false;
			bool hasProps = false;
			std::error_code ec;
			std::filesystem::path pak(appDir + "/main.pak");
			if (std::filesystem::is_regular_file(pak, ec) && std::filesystem::file_size(pak, ec) >= 1024 * 1024)
				hasPak = true;

			std::filesystem::path prop(appDir + "/properties/resources.xml");
			if (std::filesystem::is_regular_file(prop, ec))
				hasProps = true;

			std::string json = "{\"has_main_pak\":" + std::string(hasPak ? "true" : "false") +
			                   ",\"has_properties\":" + std::string(hasProps ? "true" : "false") +
			                   ",\"ready\":" + std::string((hasPak && hasProps) ? "true" : "false") + "}";
			SendResponse(clientFd, 200, "OK", "application/json", json);
		}
		else
		{
			SendResponse(clientFd, 404, "Not Found", "text/plain", "Not Found");
		}
		CloseClient(clientFd);
		return;
	}

	if (method == "POST")
	{
		if (uri.rfind("/api/upload", 0) == 0)
		{
			std::string targetPath;
			size_t q = uri.find("path=");
			if (q != std::string::npos)
			{
				size_t amp = uri.find('&', q);
				targetPath = (amp == std::string::npos) ? uri.substr(q + 5) : uri.substr(q + 5, amp - (q + 5));
				targetPath = UrlDecode(targetPath);
			}

			// Normalize path and sanitize
			std::replace(targetPath.begin(), targetPath.end(), '\\', '/');
			if (targetPath.empty() || targetPath.find("..") != std::string::npos || targetPath.front() == '/')
			{
				SendResponse(clientFd, 400, "Bad Request", "application/json", "{\"error\":\"Invalid path\"}");
				CloseClient(clientFd);
				return;
			}

			// Ensure lowercase properties/ directory for Linux case-sensitivity
			std::string lowerPath = targetPath;
			std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
			if (lowerPath == "main.pak")
			{
				targetPath = "main.pak";
			}
			else if (lowerPath.rfind("properties/", 0) == 0)
			{
				targetPath = "properties/" + targetPath.substr(11);
			}
			else if (lowerPath == "resources.xml" || lowerPath.rfind("/resources.xml") != std::string::npos)
			{
				targetPath = "properties/resources.xml";
			}

			// Parse Content-Length
			size_t contentLength = 0;
			size_t clPos = headerStr.find("Content-Length:");
			if (clPos == std::string::npos)
				clPos = headerStr.find("content-length:");
			if (clPos != std::string::npos)
			{
				size_t clEnd = headerStr.find("\r\n", clPos);
				if (clEnd == std::string::npos) clEnd = headerStr.find('\n', clPos);
				std::string clVal = headerStr.substr(clPos + 15, clEnd - (clPos + 15));
				contentLength = std::strtoul(clVal.c_str(), nullptr, 10);
			}

			std::filesystem::path destPath = std::filesystem::path(appDir) / targetPath;
			std::error_code ec;
			std::filesystem::create_directories(destPath.parent_path(), ec);

			std::filesystem::path tempPath = destPath.string() + ".tmp";
			std::ofstream outFile(tempPath, std::ios::out | std::ios::binary | std::ios::trunc);
			if (!outFile)
			{
				SendResponse(clientFd, 500, "Internal Error", "application/json", "{\"error\":\"Cannot open file\"}");
				CloseClient(clientFd);
				return;
			}

			size_t bodyStart = headerEnd + headerSepLen;
			size_t initialBodyBytes = (totalRead > static_cast<ssize_t>(bodyStart)) ? (totalRead - bodyStart) : 0;
			size_t totalWritten = 0;

			if (initialBodyBytes > 0)
			{
				size_t toWrite = std::min(initialBodyBytes, contentLength);
				outFile.write(headerBuf.data() + bodyStart, toWrite);
				totalWritten += toWrite;
			}

			SetStatus("Receiving: " + destPath.filename().string() + "...");

			std::vector<char> chunk(65536);
			while (totalWritten < contentLength)
			{
				struct pollfd pfd{};
				pfd.fd = clientFd;
				pfd.events = POLLIN;
				int pr = poll(&pfd, 1, 10000);
				if (pr <= 0) break;

				size_t need = std::min(chunk.size(), contentLength - totalWritten);
				ssize_t n = recv(clientFd, chunk.data(), need, 0);
				if (n <= 0) break;

				outFile.write(chunk.data(), n);
				totalWritten += n;
			}

			outFile.close();

			if (totalWritten == contentLength)
			{
				std::filesystem::rename(tempPath, destPath, ec);
				SetStatus("Installed: " + destPath.filename().string());
				SendResponse(clientFd, 200, "OK", "application/json", "{\"success\":true}");
			}
			else
			{
				std::filesystem::remove(tempPath, ec);
				SetStatus("Upload error: " + destPath.filename().string());
				SendResponse(clientFd, 500, "Incomplete Upload", "application/json", "{\"error\":\"Incomplete upload\"}");
			}
			CloseClient(clientFd);
			return;
		}
		else if (uri == "/api/launch")
		{
			gLaunchRequested = true;
			SendResponse(clientFd, 200, "OK", "application/json", "{\"success\":true}");
			CloseClient(clientFd);
			return;
		}
	}

	SendResponse(clientFd, 404, "Not Found", "text/plain", "Not Found");
	CloseClient(clientFd);
}

static bool CheckAndCopyFromUsb(const std::string& appDir, std::string& outStatus)
{
	static const std::vector<std::string> searchBases = {
		"/media/usb",
		"/media/usb0",
		"/media/usb1",
		"/media/usb2",
		"/media/usb3",
		"/tmp/usb",
		"/tmp/media/usb",
		"/var/run/media",
		"/media/multimedia",
		"/media/developer"
	};

	std::error_code ec;
	for (const auto& base : searchBases)
	{
		if (!std::filesystem::exists(base, ec)) continue;

		try
		{
			for (std::filesystem::recursive_directory_iterator it(base, std::filesystem::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
			{
				if (ec) break;
				if (it.depth() > 3)
				{
					it.pop();
					continue;
				}

				const auto& entry = *it;
				if (entry.is_regular_file(ec) && entry.path().filename() == "main.pak")
				{
					if (entry.file_size(ec) >= 1024 * 1024)
					{
						outStatus = "Found main.pak on USB! Installing...";
						std::filesystem::path destPak = std::filesystem::path(appDir) / "main.pak";
						std::filesystem::copy_file(entry.path(), destPak, std::filesystem::copy_options::overwrite_existing, ec);

						std::filesystem::path propDir = entry.path().parent_path() / "properties";
						if (std::filesystem::is_directory(propDir, ec))
						{
							std::filesystem::path destProp = std::filesystem::path(appDir) / "properties";
							std::filesystem::copy(propDir, destProp, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
						}

						if (WebOSHasGameResources())
						{
							outStatus = "USB install successful!";
							return true;
						}
					}
				}
			}
		}
		catch (...)
		{
		}
	}
	return false;
}

static void HttpServerWorker(int serverFd, std::string appDir)
{
	while (gServerRunning)
	{
		struct pollfd pfd{};
		pfd.fd = serverFd;
		pfd.events = POLLIN;
		int pr = poll(&pfd, 1, 200);
		if (pr > 0 && (pfd.revents & POLLIN))
		{
			struct sockaddr_in clientAddr{};
			socklen_t clientLen = sizeof(clientAddr);
			int clientFd = accept(serverFd, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);
			if (clientFd >= 0)
			{
				HandleClient(clientFd, appDir);
			}
		}
	}
}

// OpenGL ES 2.0 UI Renderer with SonicWebMania 8x8 font
struct Vertex
{
	float x, y;
	float u, v;
	float r, g, b, a;
};

class SetupRenderer
{
public:
	GLuint mProgram = 0;
	GLuint mTexture = 0;
	GLuint mVbo = 0;
	GLint mAttrPos = -1;
	GLint mAttrUv = -1;
	GLint mAttrColor = -1;
	GLint mUniformTex = -1;
	std::vector<Vertex> mVertices;

	bool Init()
	{
		const char* vsSrc =
			"attribute vec2 a_pos;\n"
			"attribute vec2 a_uv;\n"
			"attribute vec4 a_color;\n"
			"varying vec2 v_uv;\n"
			"varying vec4 v_color;\n"
			"void main() {\n"
			"    gl_Position = vec4((a_pos.x / 1920.0) * 2.0 - 1.0, 1.0 - (a_pos.y / 1080.0) * 2.0, 0.0, 1.0);\n"
			"    v_uv = a_uv;\n"
			"    v_color = a_color;\n"
			"}\n";

		const char* fsSrc =
			"precision mediump float;\n"
			"varying vec2 v_uv;\n"
			"varying vec4 v_color;\n"
			"uniform sampler2D u_tex;\n"
			"void main() {\n"
			"    float a = texture2D(u_tex, v_uv).a;\n"
			"    gl_FragColor = vec4(v_color.rgb, v_color.a * a);\n"
			"}\n";

		GLuint vs = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vs, 1, &vsSrc, nullptr);
		glCompileShader(vs);

		GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fs, 1, &fsSrc, nullptr);
		glCompileShader(fs);

		mProgram = glCreateProgram();
		glAttachShader(mProgram, vs);
		glAttachShader(mProgram, fs);
		glLinkProgram(mProgram);

		glDeleteShader(vs);
		glDeleteShader(fs);

		mAttrPos = glGetAttribLocation(mProgram, "a_pos");
		mAttrUv = glGetAttribLocation(mProgram, "a_uv");
		mAttrColor = glGetAttribLocation(mProgram, "a_color");
		mUniformTex = glGetUniformLocation(mProgram, "u_tex");

		// Build 128x128 Alpha font atlas from swm::BitmapFont (8x8 glyphs)
		std::vector<uint8_t> atlas(128 * 128, 0);
		atlas[0] = 255; // White pixel at (0, 0) for drawing solid color rectangles

		for (int i = 0; i < swm::BitmapFont::kGlyphCount; ++i)
		{
			int col = i % 16;
			int row = i / 16;
			int startX = col * 8;
			int startY = row * 8;

			for (int r = 0; r < 8; ++r)
			{
				uint8_t bits = swm::BitmapFont::kGlyphs[i][r];
				for (int c = 0; c < 8; ++c)
				{
					if (bits & (1u << c))
					{
						atlas[(startY + r) * 128 + (startX + c)] = 255;
					}
				}
			}
		}

		glGenTextures(1, &mTexture);
		glBindTexture(GL_TEXTURE_2D, mTexture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 128, 128, 0, GL_ALPHA, GL_UNSIGNED_BYTE, atlas.data());

		glGenBuffers(1, &mVbo);
		return true;
	}

	void Destroy()
	{
		if (mVbo) { glDeleteBuffers(1, &mVbo); mVbo = 0; }
		if (mTexture) { glDeleteTextures(1, &mTexture); mTexture = 0; }
		if (mProgram) { glDeleteProgram(mProgram); mProgram = 0; }
	}

	void Clear()
	{
		mVertices.clear();
	}

	void DrawQuad(float x0, float y0, float x1, float y1,
	              float u0, float v0, float u1, float v1,
	              float r, float g, float b, float a)
	{
		mVertices.push_back({x0, y0, u0, v0, r, g, b, a});
		mVertices.push_back({x1, y0, u1, v0, r, g, b, a});
		mVertices.push_back({x0, y1, u0, v1, r, g, b, a});

		mVertices.push_back({x1, y0, u1, v0, r, g, b, a});
		mVertices.push_back({x1, y1, u1, v1, r, g, b, a});
		mVertices.push_back({x0, y1, u0, v1, r, g, b, a});
	}

	void DrawRect(float x, float y, float w, float h, float r, float g, float b, float a)
	{
		float u = 0.5f / 128.0f;
		float v = 0.5f / 128.0f;
		DrawQuad(x, y, x + w, y + h, u, v, u, v, r, g, b, a);
	}

	void DrawText(const std::string& str, float x, float y, float scale,
	              float r, float g, float b, float a)
	{
		float curX = x;
		for (char ch : str)
		{
			unsigned char c = static_cast<unsigned char>(ch);
			if (c < 32 || c > 126) c = ' ';

			int idx = c - 32;
			int col = idx % 16;
			int row = idx / 16;

			float u0 = (col * 8.0f) / 128.0f;
			float v0 = (row * 8.0f) / 128.0f;
			float u1 = ((col + 1) * 8.0f) / 128.0f;
			float v1 = ((row + 1) * 8.0f) / 128.0f;

			float w = 8.0f * scale;
			float h = 8.0f * scale;

			DrawQuad(curX, y, curX + w, y + h, u0, v0, u1, v1, r, g, b, a);
			curX += w;
		}
	}

	float MeasureTextWidth(const std::string& str, float scale)
	{
		return str.size() * 8.0f * scale;
	}

	void DrawTextCentered(const std::string& str, float y, float scale,
	                      float r, float g, float b, float a)
	{
		float w = MeasureTextWidth(str, scale);
		DrawText(str, (1920.0f - w) / 2.0f, y, scale, r, g, b, a);
	}

	void Render()
	{
		if (mVertices.empty()) return;

		glUseProgram(mProgram);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, mTexture);
		glUniform1i(mUniformTex, 0);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glBindBuffer(GL_ARRAY_BUFFER, mVbo);
		glBufferData(GL_ARRAY_BUFFER, mVertices.size() * sizeof(Vertex), mVertices.data(), GL_STREAM_DRAW);

		glEnableVertexAttribArray(mAttrPos);
		glVertexAttribPointer(mAttrPos, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));

		glEnableVertexAttribArray(mAttrUv);
		glVertexAttribPointer(mAttrUv, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));

		glEnableVertexAttribArray(mAttrColor);
		glVertexAttribPointer(mAttrColor, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));

		glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mVertices.size()));

		glDisableVertexAttribArray(mAttrPos);
		glDisableVertexAttribArray(mAttrUv);
		glDisableVertexAttribArray(mAttrColor);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
};

bool WebOSRunResourceServer()
{
	signal(SIGPIPE, SIG_IGN);

	std::string appDir = GetWebOSAppDir();

	gServerRunning = true;
	gLaunchRequested = false;
	SetStatus("Waiting for upload...");

	int serverPort = 0;
	int serverFd = StartHttpServer(serverPort);
	std::string currentIP = GetLocalIPAddress();

	std::thread serverThread;
	if (serverFd >= 0)
	{
		serverThread = std::thread(HttpServerWorker, serverFd, appDir);
	}

	SDL_SetHint("SDL_APP_ID", "io.github.wszqkzqk.pvzportable");
	SDL_SetHint("SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_BACK", "true");
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");

	SDL_Window* window = SDL_CreateWindow(
		"PvZ Portable - Resource Setup",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		1920, 1080,
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN);

	if (!window)
	{
		gServerRunning = false;
		if (serverFd >= 0)
		{
			shutdown(serverFd, SHUT_RDWR);
			close(serverFd);
		}
		if (serverThread.joinable()) serverThread.join();
		return false;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

	SDL_GLContext glContext = SDL_GL_CreateContext(window);
	if (!glContext)
	{
		SDL_DestroyWindow(window);
		gServerRunning = false;
		if (serverFd >= 0)
		{
			shutdown(serverFd, SHUT_RDWR);
			close(serverFd);
		}
		if (serverThread.joinable()) serverThread.join();
		return false;
	}

	PlatformGLInit();

	SDL_GL_SetSwapInterval(1);
	glViewport(0, 0, 1920, 1080);

	SetupRenderer renderer;
	renderer.Init();

	bool quit = false;
	uint32_t lastNetworkCheck = 0;

	while (!gLaunchRequested && !quit)
	{
		uint32_t now = SDL_GetTicks();

		SDL_Event ev;
		while (SDL_PollEvent(&ev))
		{
			if (ev.type == SDL_QUIT)
			{
				quit = true;
			}
			else if (ev.type == SDL_KEYDOWN)
			{
				if (ev.key.keysym.sym == SDLK_AC_BACK ||
				    ev.key.keysym.sym == SDLK_ESCAPE ||
				    ev.key.keysym.scancode == static_cast<SDL_Scancode>(482))
				{
					quit = true;
				}
			}
		}

		// Check for USB drive insertion (every 2s)
		static uint32_t lastUsbCheck = 0;
		if (now - lastUsbCheck > 2000)
		{
			lastUsbCheck = now;
			std::string usbStatus;
			if (CheckAndCopyFromUsb(appDir, usbStatus))
			{
				SetStatus(usbStatus);
				gLaunchRequested = true;
			}
		}

		// Update network IP periodically without closing listening socket
		if (now - lastNetworkCheck > 1500)
		{
			lastNetworkCheck = now;
			std::string newIP = GetLocalIPAddress();
			if (!newIP.empty())
			{
				currentIP = newIP;
			}
		}

		// Draw Frame
		glClearColor(0.106f, 0.369f, 0.125f, 1.0f); // #1b5e20 PvZ green
		glClear(GL_COLOR_BUFFER_BIT);

		renderer.Clear();

		// Title & Subtitle
		renderer.DrawTextCentered("PvZ Portable", 70.0f, 5.0f, 1.0f, 0.835f, 0.31f, 1.0f); // #ffd54f
		renderer.DrawTextCentered("Game Resources Required", 130.0f, 2.5f, 1.0f, 1.0f, 1.0f, 0.9f);

		// Center Card (w: 1400, h: 650)
		float cardX = 260.0f;
		float cardY = 190.0f;
		float cardW = 1400.0f;
		float cardH = 660.0f;

		// Card background & border
		renderer.DrawRect(cardX - 4.0f, cardY - 4.0f, cardW + 8.0f, cardH + 8.0f, 0.18f, 0.49f, 0.196f, 1.0f); // #2e7d32
		renderer.DrawRect(cardX, cardY, cardW, cardH, 0.063f, 0.208f, 0.078f, 1.0f); // #103514

		if (!currentIP.empty() && serverFd >= 0)
		{
			std::string urlStr = "http://" + currentIP + ":" + std::to_string(serverPort);

			renderer.DrawTextCentered("To install game files, open this address in a browser", cardY + 40.0f, 2.2f, 0.85f, 0.9f, 0.92f, 1.0f);
			renderer.DrawTextCentered("on your phone, tablet, or computer (on the same Wi-Fi):", cardY + 75.0f, 2.2f, 0.75f, 0.82f, 0.85f, 1.0f);

			// Big URL Box
			float urlBoxW = 1100.0f;
			float urlBoxH = 90.0f;
			float urlBoxX = (1920.0f - urlBoxW) / 2.0f;
			float urlBoxY = cardY + 120.0f;

			renderer.DrawRect(urlBoxX - 3.0f, urlBoxY - 3.0f, urlBoxW + 6.0f, urlBoxH + 6.0f, 0.505f, 0.78f, 0.518f, 1.0f); // #81c784
			renderer.DrawRect(urlBoxX, urlBoxY, urlBoxW, urlBoxH, 0.035f, 0.12f, 0.047f, 1.0f); // #091f0c

			renderer.DrawTextCentered(urlStr, urlBoxY + 25.0f, 4.5f, 1.0f, 0.933f, 0.345f, 1.0f); // #ffee58

			renderer.DrawTextCentered("Upload main.pak and properties/ folder (or a .zip archive).", cardY + 245.0f, 2.2f, 0.75f, 0.88f, 0.75f, 1.0f);
			renderer.DrawTextCentered("OR: Simply plug a USB flash drive with main.pak into the TV!", cardY + 295.0f, 2.4f, 0.45f, 0.95f, 0.45f, 1.0f);
			renderer.DrawTextCentered("(USB drive is auto-detected, copied, and starts instantly)", cardY + 340.0f, 2.0f, 0.7f, 0.85f, 0.7f, 1.0f);

			// Status Box
			float stBoxW = 900.0f;
			float stBoxH = 50.0f;
			float stBoxX = (1920.0f - stBoxW) / 2.0f;
			float stBoxY = cardY + 410.0f;
			renderer.DrawRect(stBoxX, stBoxY, stBoxW, stBoxH, 0.1f, 0.28f, 0.12f, 0.8f);

			std::string statusMsg = GetStatus();
			renderer.DrawTextCentered("Status: " + statusMsg, stBoxY + 16.0f, 2.2f, 1.0f, 0.96f, 0.62f, 1.0f);

			renderer.DrawTextCentered("The game starts automatically once files are installed.", cardY + 510.0f, 2.2f, 0.8f, 0.9f, 0.85f, 1.0f);
			renderer.DrawTextCentered("Ready check: " + std::string(WebOSHasGameResources() ? "READY TO PLAY" : "Awaiting main.pak + properties/"),
			                          cardY + 560.0f, 2.2f, WebOSHasGameResources() ? 0.41f : 0.8f, WebOSHasGameResources() ? 0.94f : 0.6f, 0.4f, 1.0f);
		}
		else
		{
			// Offline screen
			renderer.DrawTextCentered("[ ! ] No Network Connection Detected", cardY + 100.0f, 3.0f, 1.0f, 0.34f, 0.13f, 1.0f); // #ff5722

			renderer.DrawTextCentered("Please connect your TV to Wi-Fi or Ethernet", cardY + 200.0f, 2.5f, 1.0f, 1.0f, 1.0f, 1.0f);
			renderer.DrawTextCentered("in your webOS TV Network Settings.", cardY + 250.0f, 2.5f, 1.0f, 1.0f, 1.0f, 1.0f);

			renderer.DrawTextCentered("OR: Simply plug a USB flash drive with main.pak into the TV!", cardY + 340.0f, 2.4f, 0.45f, 0.95f, 0.45f, 1.0f);
			renderer.DrawTextCentered("(USB drive will be automatically detected and installed)", cardY + 390.0f, 2.0f, 0.7f, 0.85f, 0.7f, 1.0f);

			int dots = (now / 500) % 4;
			std::string dotsStr = "Checking network connection" + std::string(dots, '.');
			renderer.DrawTextCentered(dotsStr, cardY + 500.0f, 2.2f, 1.0f, 0.8f, 0.5f, 1.0f);
		}

		// Footer
		renderer.DrawTextCentered("Press Back button on remote to exit", 940.0f, 2.0f, 0.47f, 0.56f, 0.61f, 1.0f);

		renderer.Render();
		SDL_GL_SwapWindow(window);
		SDL_Delay(16);
	}

	bool launched = gLaunchRequested.load();

	if (launched)
	{
		// Render launching splash
		glClearColor(0.106f, 0.369f, 0.125f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		renderer.Clear();
		renderer.DrawTextCentered("PvZ Portable", 440.0f, 5.0f, 1.0f, 0.835f, 0.31f, 1.0f);
		renderer.DrawTextCentered("Starting game...", 530.0f, 3.0f, 0.5f, 0.95f, 0.5f, 1.0f);
		renderer.Render();
		SDL_GL_SwapWindow(window);
		SDL_Delay(600);
	}

	// Stop server thread
	gServerRunning = false;
	if (serverFd >= 0)
	{
		shutdown(serverFd, SHUT_RDWR);
		close(serverFd);
		serverFd = -1;
	}
	if (serverThread.joinable())
	{
		serverThread.join();
	}

	renderer.Destroy();
	SDL_GL_DeleteContext(glContext);
	SDL_DestroyWindow(window);

	return launched;
}

} // namespace Sexy
