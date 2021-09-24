#include <Windows.h>
#include <string>
#include <thread>
#include <vector>

#include "GameClasses.hpp"
#include "Memory.hpp"
#include "Offsets.hpp"

#define SEE_ERROR 0

std::uint8_t* find_signature(const wchar_t* szModule, const char* szSignature) noexcept
{
	try {
		auto module{ ::GetModuleHandleW(szModule) };
		static auto pattern_to_byte = [](const char* pattern) {
			auto bytes{ std::vector<std::int32_t>{} };
			auto* start{ const_cast<char*>(pattern) };
			auto* end{ const_cast<char*>(pattern) + ::strlen(pattern) };

			for (auto* current{ start }; current < end; ++current) {
				if (*current == '?') {
					++current;
					if (*current == '?')
						++current;
					bytes.push_back(-1);
				} else
					bytes.push_back(::strtoul(current, &current, 16));
			}
			return bytes;
		};

		auto dosHeader{ reinterpret_cast<PIMAGE_DOS_HEADER>(module) };
		if (dosHeader == NULL)
			return nullptr;

		auto ntHeaders{ reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<std::uint8_t*>(module + dosHeader->e_lfanew)) };
		auto textSection{ IMAGE_FIRST_SECTION(ntHeaders) };
		auto sizeOfImage{ textSection->SizeOfRawData };
		auto patternBytes{ pattern_to_byte(szSignature) };
		auto* scanBytes{ reinterpret_cast<std::uint8_t*>(module) + textSection->VirtualAddress };
		auto s{ patternBytes.size() };
		auto* d{ patternBytes.data() };
		auto mbi{ MEMORY_BASIC_INFORMATION{ 0 } };
		std::uint8_t* next_check_address{ 0 };

		for (auto i{ 0ul }; i < sizeOfImage - s; ++i) {
			bool found{ true };
			for (auto j{ 0ul }; j < s; ++j) {
				auto* current_address{ scanBytes + i + j };
				if (current_address >= next_check_address) {
					if (!::VirtualQuery(reinterpret_cast<void*>(current_address), &mbi, sizeof(mbi)))
						break;

					if (mbi.Protect == PAGE_NOACCESS) {
						i += ((reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) - (reinterpret_cast<std::uintptr_t>(scanBytes) + i));
						i--;
						found = false;
						break;
					} else
						next_check_address = reinterpret_cast<std::uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
				}

				if (scanBytes[i + j] != d[j] && d[j] != -1) {
					found = false;
					break;
				}
			}
			if (found)
				return &scanBytes[i];
		}
		return nullptr;
	} catch (const std::exception&) {
		return nullptr;
	}
}

class offset_signature {
public:
	std::string pattern;
	bool sub_base;
	bool read;
	std::uintptr_t* offset;
};

std::vector<offset_signature> gameClientSig{ { "A1 ? ? ? ? 68 ? ? ? ? 8B 70 08", true, true, &offsets::global::GameClient } };

std::vector<offset_signature> sigs{ 
	{ "A1 ? ? ? ? 85 C0 74 07 05 ? ? ? ? EB 02 33 C0 56", true, true, &offsets::global::Player },
	{ "8B 0D ? ? ? ? 50 8D 44 24 18", true, true, &offsets::global::ManagerTemplate_AIHero_ },
	{ "89 1D ? ? ? ? 57 8D 4B 04", true, true, &offsets::global::ChampionManager },
	{ "A1 ? ? ? ? 53 55 8B 6C 24 1C", true, true, &offsets::global::ManagerTemplate_AIMinionClient_ },
	{ "3B 05 ? ? ? ? 75 72", true, true, &offsets::global::Riot__g_window },
	{ "8D 8E ? ? ? ? FF 74 24 4C", false, true, &offsets::AIBaseCommon::CharacterDataStack },
	{ "80 BE ? ? ? ? ? 75 4D 0F 31 33 C9 66 C7 86 ? ? ? ? ? ? 89 44 24 18 33 FF", false, true, &offsets::AIBaseCommon::SkinId },
	{ "8B 86 ? ? ? ? 89 4C 24 08", false, true, &offsets::MaterialRegistry::D3DDevice },
	{ "8B 8E ? ? ? ? 52 57", false, true, &offsets::MaterialRegistry::SwapChain },
	{ "83 EC 50 53 55 56 57 8B F9 8B 47 04", true, false, &offsets::functions::CharacterDataStack__Push },
	{ "83 EC 1C 56 57 8D 44 24 20", true, false, &offsets::functions::CharacterDataStack__Update },
	{ "A1 ? ? ? ? 85 C0 75 0B 8B 0D ? ? ? ? 8B 01 FF 60 14", true, false, &offsets::functions::Riot__Renderer__MaterialRegistry__GetSingletonPtr },
	{ "E8 ? ? ? ? 8B 0D ? ? ? ? 83 C4 04 8B F0 6A 0B", true, false, &offsets::functions::translateString_UNSAFE_DONOTUSE },
	{ "E8 ? ? ? ? 39 44 24 1C 5F", true, false, &offsets::functions::GetGoldRedirectTarget }
};

void Memory::Search(bool gameClient) noexcept
{
	try {
		auto base{ reinterpret_cast<std::uintptr_t>(::GetModuleHandleA(nullptr)) };
		auto& signatureToSearch{ (gameClient ? gameClientSig : sigs) };

		for (auto& sig : signatureToSearch)
			*sig.offset = 0;

		while (true) {
			bool missing_offset{ false };
			for (auto& sig : signatureToSearch) {
				if (*sig.offset != 0)
					continue;

				auto* address{ find_signature(nullptr, sig.pattern.c_str()) };

				if (address == nullptr) {
#if SEE_ERROR
					::MessageBoxA(nullptr, sig.pattern.c_str(), "R3nzSkin", MB_OK | MB_ICONWARNING);
#endif
				} else {
					if (sig.read)
						address = *reinterpret_cast<std::uint8_t**>(address + (sig.pattern.find_first_of("?") / 3));
					else if (address[0] == 0xE8)
						address = address + *reinterpret_cast<std::uint32_t*>(address + 1) + 5;

					if (sig.sub_base)
						address -= base;

					*sig.offset = reinterpret_cast<std::uint32_t>(address);
					continue;
				}

				if (!*sig.offset) {
					missing_offset = true;
					continue;
				}
			}

			if (!missing_offset)
				break;

			std::this_thread::sleep_for(2s);
		}
#if SEE_ERROR
	} catch (const std::exception& e) {
		MessageBoxA(nullptr, e.what(), "R3nzSkin", MB_OK | MB_ICONWARNING);
#else
	} catch (const std::exception&) {
#endif
	}
}