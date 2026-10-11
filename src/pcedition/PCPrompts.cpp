#include "pcedition/PCPrompts.h"
#include "pcedition/PCEdition.h"

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <boost/algorithm/string.hpp>

namespace pce::Prompts
{
	namespace
	{
		bool sActive = false;
		std::vector<uint8> sTemplate; // prompt font (glyph lookup for KeyGlyph)
		std::unordered_map<uint16, uint16> sTemplateCmap;

		// ---------------------------------------------------------------- byte helpers (big endian)
		uint16 RB16(const uint8* p) { return (uint16)((p[0] << 8) | p[1]); }
		uint32 RB32(const uint8* p) { return ((uint32)p[0] << 24) | ((uint32)p[1] << 16) | ((uint32)p[2] << 8) | p[3]; }
		void WB16(uint8* p, uint16 v) { p[0] = (uint8)(v >> 8); p[1] = (uint8)v; }
		void WB32(uint8* p, uint32 v) { p[0] = (uint8)(v >> 24); p[1] = (uint8)(v >> 16); p[2] = (uint8)(v >> 8); p[3] = (uint8)v; }

		bool ReadFile(const fs::path& p, std::vector<uint8>& out)
		{
			std::ifstream f(p, std::ios::binary);
			if (!f)
				return false;
			f.seekg(0, std::ios::end);
			out.resize((size_t)f.tellg());
			f.seekg(0);
			f.read((char*)out.data(), out.size());
			return (bool)f;
		}

		bool WriteFileAtomic(const fs::path& p, const std::vector<uint8>& data)
		{
			std::error_code ec;
			fs::create_directories(p.parent_path(), ec);
			fs::path tmp = p;
			tmp += ".tmp";
			{
				std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
				if (!f)
					return false;
				f.write((const char*)data.data(), data.size());
				if (!f)
					return false;
			}
			fs::rename(tmp, p, ec);
			return !ec;
		}

		// ---------------------------------------------------------------- Yaz0
		bool Yaz0Decode(const std::vector<uint8>& in, std::vector<uint8>& out)
		{
			if (in.size() < 16 || memcmp(in.data(), "Yaz0", 4) != 0)
				return false;
			const uint32 size = RB32(in.data() + 4);
			out.assign(size, 0);
			size_t src = 16, dst = 0;
			while (dst < size && src < in.size())
			{
				const uint8 flags = in[src++];
				for (int bit = 7; bit >= 0 && dst < size; bit--)
				{
					if (flags & (1 << bit))
					{
						if (src >= in.size())
							return false;
						out[dst++] = in[src++];
					}
					else
					{
						if (src + 1 >= in.size())
							return false;
						const uint8 b1 = in[src++], b2 = in[src++];
						const size_t dist = ((size_t)(b1 & 0x0F) << 8 | b2) + 1;
						size_t len = b1 >> 4;
						if (len == 0)
						{
							if (src >= in.size())
								return false;
							len = (size_t)in[src++] + 0x12;
						}
						else
							len += 2;
						if (dist > dst)
							return false;
						for (size_t i = 0; i < len && dst < size; i++, dst++)
							out[dst] = out[dst - dist];
					}
				}
			}
			return dst == size;
		}

		// literal-only Yaz0 (valid for every decoder; the game decompresses it as usual)
		std::vector<uint8> Yaz0EncodeStore(const std::vector<uint8>& in, uint32 alignment)
		{
			std::vector<uint8> out;
			out.reserve(16 + in.size() + in.size() / 8 + 8);
			out.resize(16, 0);
			memcpy(out.data(), "Yaz0", 4);
			WB32(out.data() + 4, (uint32)in.size());
			WB32(out.data() + 8, alignment);
			for (size_t i = 0; i < in.size(); i += 8)
			{
				out.push_back(0xFF);
				const size_t n = std::min<size_t>(8, in.size() - i);
				out.insert(out.end(), in.begin() + i, in.begin() + i + n);
			}
			return out;
		}

		// ---------------------------------------------------------------- SARC (big endian, Wii U)
		struct SarcFile
		{
			uint32 hash;
			uint32 attr;
			std::string name;
			std::vector<uint8> data;
		};

		bool SarcRead(const std::vector<uint8>& d, std::vector<SarcFile>& files, uint32& hashKey)
		{
			if (d.size() < 0x20 || memcmp(d.data(), "SARC", 4) != 0 || RB16(d.data() + 6) != 0xFEFF)
				return false;
			const uint32 dataOff = RB32(d.data() + 0x0C);
			const uint8* sfat = d.data() + RB16(d.data() + 4);
			if (memcmp(sfat, "SFAT", 4) != 0)
				return false;
			const uint16 count = RB16(sfat + 6);
			hashKey = RB32(sfat + 8);
			const uint8* nodes = sfat + 0x0C;
			const uint8* sfnt = nodes + count * 0x10;
			if (memcmp(sfnt, "SFNT", 4) != 0)
				return false;
			const uint8* names = sfnt + RB16(sfnt + 4);
			files.clear();
			for (uint16 i = 0; i < count; i++)
			{
				const uint8* n = nodes + i * 0x10;
				SarcFile f;
				f.hash = RB32(n);
				f.attr = RB32(n + 4);
				const uint32 b = RB32(n + 8), e = RB32(n + 12);
				if (f.attr & 0x01000000)
					f.name = (const char*)(names + (f.attr & 0xFFFF) * 4);
				if (dataOff + e > d.size() || b > e)
					return false;
				f.data.assign(d.begin() + dataOff + b, d.begin() + dataOff + e);
				files.push_back(std::move(f));
			}
			return true;
		}

		std::vector<uint8> SarcWrite(const std::vector<SarcFile>& files, uint32 hashKey, uint32 alignment)
		{
			// names table
			std::vector<uint8> names;
			std::vector<uint32> nameOffs;
			for (auto& f : files)
			{
				nameOffs.push_back((uint32)names.size());
				names.insert(names.end(), f.name.begin(), f.name.end());
				names.push_back(0);
				while (names.size() % 4)
					names.push_back(0);
			}
			const uint32 headerSize = 0x14 + 0x0C + (uint32)files.size() * 0x10 + 8 + (uint32)names.size();
			const uint32 dataOff = (headerSize + alignment - 1) & ~(alignment - 1);
			std::vector<uint8> out(dataOff, 0);
			// data
			std::vector<std::pair<uint32, uint32>> ranges;
			for (auto& f : files)
			{
				size_t pos = out.size();
				pos = (pos + alignment - 1) & ~(size_t)(alignment - 1);
				out.resize(pos, 0);
				ranges.emplace_back((uint32)(pos - dataOff), (uint32)(pos - dataOff + f.data.size()));
				out.insert(out.end(), f.data.begin(), f.data.end());
			}
			uint8* p = out.data();
			memcpy(p, "SARC", 4);
			WB16(p + 4, 0x14);
			WB16(p + 6, 0xFEFF);
			WB32(p + 8, (uint32)out.size());
			WB32(p + 0x0C, dataOff);
			WB16(p + 0x10, 0x0100);
			uint8* sfat = p + 0x14;
			memcpy(sfat, "SFAT", 4);
			WB16(sfat + 4, 0x0C);
			WB16(sfat + 6, (uint16)files.size());
			WB32(sfat + 8, hashKey);
			for (size_t i = 0; i < files.size(); i++)
			{
				uint8* n = sfat + 0x0C + i * 0x10;
				WB32(n, files[i].hash);
				WB32(n + 4, files[i].name.empty() ? 0 : (0x01000000 | (nameOffs[i] / 4)));
				WB32(n + 8, ranges[i].first);
				WB32(n + 12, ranges[i].second);
			}
			uint8* sfnt = sfat + 0x0C + files.size() * 0x10;
			memcpy(sfnt, "SFNT", 4);
			WB16(sfnt + 4, 8);
			memcpy(sfnt + 8, names.data(), names.size());
			return out;
		}

		// ---------------------------------------------------------------- CRC32 (RSTB keys)
		uint32 Crc32(std::string_view s)
		{
			static uint32 table[256];
			static bool init = false;
			if (!init)
			{
				for (uint32 i = 0; i < 256; i++)
				{
					uint32 c = i;
					for (int k = 0; k < 8; k++)
						c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
					table[i] = c;
				}
				init = true;
			}
			uint32 crc = 0xFFFFFFFFu;
			for (char ch : s)
				crc = table[(crc ^ (uint8)ch) & 0xFF] ^ (crc >> 8);
			return crc ^ 0xFFFFFFFFu;
		}

		// ---------------------------------------------------------------- prompt font
		void ParseTemplateCmap()
		{
			sTemplateCmap.clear();
			const auto& d = sTemplate;
			if (d.size() < 0x34 || memcmp(d.data(), "FFNT", 4) != 0)
				return;
			uint32 cmap = RB32(d.data() + 0x14 + 0x1C); // FINF ptrCMAP (points at block data)
			for (int guard = 0; cmap && cmap + 12 < d.size() && guard < 64; guard++)
			{
				const uint8* c = d.data() + cmap;
				const uint16 lo = RB16(c), hi = RB16(c + 2), method = RB16(c + 4);
				if (method == 1)
					for (uint32 code = lo; code <= hi; code++)
					{
						const uint16 g = RB16(c + 12 + (code - lo) * 2);
						if (g != 0xFFFF)
							sTemplateCmap[(uint16)code] = g;
					}
				cmap = RB32(c + 8);
			}
		}

		// the font's CMAP table for U+E040..U+E08F (game button codes)
		uint8* FindBaseTable(std::vector<uint8>& d)
		{
			uint32 cmap = RB32(d.data() + 0x14 + 0x1C);
			for (int guard = 0; cmap && cmap + 12 < d.size() && guard < 64; guard++)
			{
				uint8* c = d.data() + cmap;
				if (RB16(c) == 0xE040 && RB16(c + 4) == 1)
					return c + 12;
				cmap = RB32(c + 8);
			}
			return nullptr;
		}

		std::string BindingStamp(const Settings& s)
		{
			std::ostringstream o;
			for (uint16 c = 0xE040; c <= 0xE067; c++)
				o << std::hex << DefaultGlyphFor(s, c) << ",";
			return o.str();
		}
	}

	uint16 KeyGlyph(Key k)
	{
		if (k == Key::None)
			return 0;
		const uint16 code = (uint16)(0xE100 + (uint16)k);
		if (!sTemplateCmap.empty() && !sTemplateCmap.contains(code))
			return 0;
		return code;
	}

	uint16 ActionGlyph(const Settings& s, Action a)
	{
		const auto& b = s.bind(a);
		for (Key k : b.keys)
			if (uint16 g = KeyGlyph(k))
				return g;
		return 0;
	}

	uint16 DefaultGlyphFor(const Settings& s, uint16 code)
	{
		auto key = [&](Action a, uint16 fallback) {
			uint16 g = ActionGlyph(s, a);
			return g ? g : fallback;
		};
		const bool wasdDefault = s.bind(Action::MoveForward).keys[0] == KeyChar('W') && s.bind(Action::MoveLeft).keys[0] == KeyChar('A') &&
								 s.bind(Action::MoveBack).keys[0] == KeyChar('S') && s.bind(Action::MoveRight).keys[0] == KeyChar('D');
		switch (code)
		{
		case 0xE040: return key(Action::Interact, code);
		case 0xE041: return key(Action::Sprint, code);
		case 0xE042: return key(Action::Jump, code);
		case 0xE043: return key(Action::Attack, code);
		case 0xE044: return key(Action::UseRune, code);
		case 0xE045: return key(Action::Throw, code);
		case 0xE046: return key(Action::Target, code);
		case 0xE047: return key(Action::Bow, code);
		case 0xE048: return KeyGlyph(Key::Escape);
		case 0xE049: return kGlyphOneToFour;
		case 0xE04A: return KeyGlyph(Key::Escape);
		case 0xE04B: return key(Action::Inventory, code);
		case 0xE04C: return key(Action::Map, code);
		case 0xE050: return key(Action::MoveBack, code);
		case 0xE051: return kGlyphMouseDown;
		case 0xE052: return key(Action::MoveForward, code);
		case 0xE053: return kGlyphMouseUp;
		case 0xE054: return wasdDefault ? kGlyphAD : key(Action::MoveRight, code);
		case 0xE055: return kGlyphMouseX;
		case 0xE056: return key(Action::Crouch, code);
		case 0xE057: return key(Action::Scope, code);
		case 0xE058: return key(Action::MoveRight, code);
		case 0xE059: return kGlyphMouseRight;
		case 0xE05A: return key(Action::MoveLeft, code);
		case 0xE05B: return kGlyphMouseLeft;
		case 0xE05D: return kGlyphMouseY;
		case 0xE05E: return wasdDefault ? kGlyphWASD : key(Action::MoveForward, code);
		case 0xE05F: return kGlyphMouseMove;
		case 0xE060: return key(Action::RuneSelect, code);
		case 0xE061: return key(Action::Whistle, code);
		case 0xE062: return key(Action::ShieldSelect, code);
		case 0xE063: return key(Action::WeaponSelect, code);
		case 0xE064: return kGlyphWheelUpDown;
		case 0xE065: return kGlyphArrowKeys;
		case 0xE066: return key(Action::TiltMode, code);
		default: return code;
		}
	}

	bool Active()
	{
		return sActive;
	}

	void Prepare(const fs::path& packDir)
	{
		std::error_code ec;
		// the font archive is named after the region of the game
		std::string fontName = "Font_EU";
		for (const char* n : {"Font_EU", "Font_US", "Font_JP"})
		{
			if (!GameFileStamp(fmt::format("content/Font/{}.sbfarc", n)).empty())
			{
				fontName = n;
				break;
			}
		}
		const fs::path fontOut = packDir / "content" / "Font" / (fontName + ".sbfarc");
		const fs::path rstbOut = packDir / "content" / "System" / "Resource" / "ResourceSizeTable.product.srsizetable";
		const fs::path stampPath = packDir / "prompts.stamp";
		const Settings s = GetSettings();
		if (!ReadFile(GetExeDir() / "resources" / "pcedition" / "External_00.bffnt", sTemplate))
		{
			cemuLog_log(LogType::Force, "PCE prompts: template font missing, keeping GamePad prompts");
			fs::remove(fontOut, ec);
			fs::remove(rstbOut, ec);
			return;
		}
		ParseTemplateCmap();

		// both files come from the player's dump (title folders or archive; the update has them)
		const std::string fontSrc = fmt::format("content/Font/{}.sbfarc", fontName);
		const std::string rstbSrc = "content/System/Resource/ResourceSizeTable.product.srsizetable";

		// cache: rebuild only when the bindings, the template or the source files changed
		std::ostringstream st;
		st << "v2|" << sTemplate.size() << "|" << Crc32(std::string_view((const char*)sTemplate.data(), sTemplate.size())) << "|"
		   << GameFileStamp(fontSrc) << "|" << GameFileStamp(rstbSrc) << "|" << BindingStamp(s);
		const std::string stamp = st.str();
		{
			std::vector<uint8> old;
			if (ReadFile(stampPath, old) && std::string(old.begin(), old.end()) == stamp && fs::exists(fontOut, ec) &&
				fs::exists(rstbOut, ec))
			{
				sActive = true;
				return;
			}
		}

		std::vector<uint8> raw, sarc, rstbRaw, rstb;
		std::vector<SarcFile> files;
		uint32 hashKey = 0x65;
		if (!ReadGameFile(fontSrc, raw) || !Yaz0Decode(raw, sarc) || !SarcRead(sarc, files, hashKey) ||
			!ReadGameFile(rstbSrc, rstbRaw) || !Yaz0Decode(rstbRaw, rstb) || rstb.size() < 12 || memcmp(rstb.data(), "RSTB", 4) != 0)
		{
			cemuLog_log(LogType::Force, "PCE prompts: could not read {} / {}", fontSrc, rstbSrc);
			return;
		}
		const uint32 yazAlign = RB32(raw.data() + 8);

		// prompt font with the button codes pointed at the bound keys
		std::vector<uint8> font = sTemplate;
		if (uint8* table = FindBaseTable(font))
		{
			for (uint16 code = 0xE040; code <= 0xE08F; code++)
			{
				const uint16 target = DefaultGlyphFor(s, code);
				if (target == code)
					continue; // arrows etc. keep the template's own glyphs
				auto it = sTemplateCmap.find(target);
				if (it != sTemplateCmap.end())
					WB16(table + (code - 0xE040) * 2, it->second);
			}
		}
		bool replaced = false;
		for (auto& f : files)
			if (f.name == "External_00.bffnt")
			{
				f.data = font;
				replaced = true;
			}
		if (!replaced)
		{
			cemuLog_log(LogType::Force, "PCE prompts: External_00.bffnt not found in {}", fontSrc);
			return;
		}
		const std::vector<uint8> newSarc = SarcWrite(files, hashKey, 0x2000);

		// resource size table: keep the game's overhead for this resource, grow it by our size difference
		const uint32 crcCount = RB32(rstb.data() + 4);
		const uint32 key = Crc32("Font/" + fontName + ".bfarc");
		bool rstbOk = false;
		for (uint32 i = 0; i < crcCount && 12 + (i + 1) * 8 <= rstb.size(); i++)
		{
			uint8* e = rstb.data() + 12 + i * 8;
			if (RB32(e) == key)
			{
				const sint64 overhead = (sint64)RB32(e + 4) - (sint64)sarc.size();
				const uint32 need = (uint32)((((sint64)newSarc.size() + std::max<sint64>(overhead, 0x2008)) + 31) & ~31ll);
				if (need > RB32(e + 4))
					WB32(e + 4, need);
				rstbOk = true;
				break;
			}
		}
		if (!rstbOk)
			cemuLog_log(LogType::Force, "PCE prompts: {}.bfarc has no RSTB entry (left unchanged)", fontName);

		if (!WriteFileAtomic(fontOut, Yaz0EncodeStore(newSarc, yazAlign)) ||
			!WriteFileAtomic(rstbOut, Yaz0EncodeStore(rstb, RB32(rstbRaw.data() + 8))))
		{
			cemuLog_log(LogType::Force, "PCE prompts: writing the pack content failed");
			return;
		}
		std::vector<uint8> stampData(stamp.begin(), stamp.end());
		WriteFileAtomic(stampPath, stampData);
		sActive = true;
		cemuLog_log(LogType::Force, "PCE prompts: installed keyboard & mouse prompts ({} -> {} bytes)", sarc.size(), newSarc.size());
	}
}
