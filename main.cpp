#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

#include <FileReader.hpp>
#include <filesystem.hpp>
#include <FileWriter.hpp>
#include <lookup3.hpp>
#include <Regex.hpp>
#include <riff.hpp>
#include <sha1.hpp>
#include <string.hpp>
#include <unicode.hpp>

using namespace soup;

static bool seek_sound_table(RiffReader& rr, size_t end)
{
	while (rr.r.getPosition() != end)
	{
		auto ck = rr.readChunk();
		if (ck.is_list)
		{
			if (seek_sound_table(rr, ck.getDataEnd()))
			{
				return true;
			}
		}
		else
		{
			if (ck.name == "STBL")
			{
				return true;
			}
			ck.skipData(rr.r);
		}
	}
	return false;
}

static uint32_t read_short_int(Reader& r)
{
	uint16_t s = 0;
	r.u16_le(s);
	uint32_t i = s;
	if ((s & 0x8000) != 0)
	{
		r.u16_le(s);
		i |= ((uint32_t)s) << 16;
	}
	return i;
}

struct SoundTable
{
	std::vector<std::pair<uint64_t, uint32_t>> vec;

	bool read(Reader& r)
	{
		const auto num_keys = read_short_int(r);
		vec.reserve(num_keys);
		for (uint32_t i = 0; i != num_keys; ++i)
		{
			uint64_t k;
			SOUP_RETHROW_FALSE(r.u64_le(k));
			vec.emplace_back(k, 0 /* placeholder */);
		}
		SOUP_RETHROW_FALSE(read_short_int(r) == num_keys);
		for (uint32_t i = 0; i != num_keys; ++i)
		{
			SOUP_RETHROW_FALSE(r.u24_le(vec[i].second));
		}
		return true;
	}

	void write(Writer& w)
	{
		SOUP_ASSERT(vec.size() < 0x8000);
		auto num_keys = static_cast<uint16_t>(vec.size());
		w.u16_le(num_keys);
		for (auto& e : vec)
		{
			w.u64_le(e.first);
		}
		w.u16_le(num_keys);
		for (auto& e : vec)
		{
			w.u24_le(e.second);
		}
	}
};

[[nodiscard]] static std::string sha1_file(const std::filesystem::path& path)
{
	std::string digest;
	size_t size;
	if (const void* data = filesystem::createFileMapping(path, size))
	{
		digest = sha1::hash(data, size);
		filesystem::destroyFileMapping(data, size);
	}
	return digest;
}

static bool is_valid_lang_code(const std::string& lang)
{
	return lang == "EN"
		|| lang == "JP"
		|| lang == "CN"
		|| lang == "TW"
		|| lang == "DE"
		|| lang == "IT"
		|| lang == "KO"
		|| lang == "BR"
		|| lang == "ES"
		|| lang == "MX"
		;
}

int main(int argc, const char* argv[])
{
	if (argc != 1 && argc != 3)
	{
		std::cout << "Usage:\n";
		std::cout << "- Replace: Schwap.exe <game language> <audio language>\n";
		std::cout << "- Restore: Schwap.exe\n";
		return 1;
	}

	std::string game_lang;
	std::string audio_lang;
	if (argc == 3)
	{
		game_lang = argv[1];
		if (!is_valid_lang_code(game_lang))
		{
			std::cout << "Invalid language code: " << game_lang << "\n";
			return 1;
		}
		audio_lang = argv[2];
		if (!is_valid_lang_code(audio_lang))
		{
			std::cout << "Invalid language code: " << audio_lang << "\n";
			return 1;
		}
	}

	//std::filesystem::path path = R"(D:\SteamLibrary\steamapps\common\ForzaHorizon6)";
	std::filesystem::path path = std::filesystem::current_path();
	path /= "media";
	path /= "Audio";
	if (!std::filesystem::is_directory(path / "FMODBanks"))
	{
		std::cout << "Schwap needs to be run in the ForzaHorizon6 folder (the one containing forzahorizon6.exe).\n";
		system("pause > nul");
		return 2;
	}

	std::unordered_set<std::string> strings;
	if (!audio_lang.empty()) // Replace mode?
	{
		// Build string map for hashes
		std::cout << "Indexing...\n";
		for (const auto& e : std::filesystem::directory_iterator(path))
		{
			if (e.is_regular_file())
			{
				const std::string filename = unicode::utf16_to_utf8(e.path().filename().u16string());
				std::string pattern;
				if (filename.starts_with("Dialogue_") || filename == "DialogueScript.xml")
				{
					pattern = R"EOR(\/(HZ6_.+?)")EOR";
				}
				else if (filename == "RadioInfo_EN.xml")
				{
					pattern = R"EOR("(HZ6_.+?)_EN")EOR";
				}
				else if (filename == "SatNavConfig.xml")
				{
					pattern = R"EOR("(HZ6_.+?)")EOR";
				}
				else
				{
					continue;
				}
				std::string str = string::fromFile(e.path());
				Regex r(pattern);
				size_t i = 0;
				RegexMatchResult m;
				while (m = r.search(&str.data()[i], &str.data()[str.size()]), m.isSuccess())
				{
					const size_t offset = (m.groups.at(0).value().begin - str.data());
					strings.emplace(m.groups.at(1).value().toString());
					i = offset + m.length();
				}
			}
		}
	}

	path /= "FMODBanks";

	if (std::filesystem::exists(path / "SchwapBackups"))
	{
		std::cout << "Restoring backups...\n";
		for (const auto& e : std::filesystem::directory_iterator(path / "SchwapBackups"))
		{
			if (e.is_regular_file())
			{
				auto filename = unicode::utf16_to_utf8(e.path().filename().u16string());
				if (filename.ends_with(" - replacement sha1"))
				{
					filename = filename.substr(0, filename.size() - 19);
					if (sha1_file(path / filename) == string::fromFile(e.path()))
					{
						std::filesystem::rename(path / "SchwapBackups" / (filename + " - original"), path / filename);
					}
					else
					{
						std::filesystem::remove(path / "SchwapBackups" / (filename + " - original"));
					}
					std::filesystem::remove(e.path());
				}
			}
		}
		if (audio_lang.empty()) // Restore mode?
		{
			std::error_code ec;
			std::filesystem::remove(path / "SchwapBackups", ec);
		}
	}

	if (!audio_lang.empty()) // Replace mode?
	{
		std::cout << "Building replacement map...\n";
		std::unordered_map<uint64_t, uint64_t> map;
		for (const auto& str : strings)
		{
			map.emplace(
				lookup3::hash64(str + "_" + audio_lang),
				lookup3::hash64(str + "_" + game_lang)
			);
		}
		strings = std::unordered_set<std::string>(); // no longer needed, free memory

		{
			std::error_code ec;
			std::filesystem::create_directory(path / "SchwapBackups", ec);
		}
		for (const auto& e : std::filesystem::directory_iterator(path))
		{
			if (e.is_regular_file())
			{
				const auto filename = unicode::utf16_to_utf8(e.path().filename().u16string());
				if (filename.ends_with("_" + audio_lang + ".assets.bank"))
				{
					const auto bank_name = filename.substr(0, filename.size() - 15);
					const auto dest_file = path / (bank_name + "_" + game_lang + ".assets.bank");

					std::cout << "Processing " << bank_name << "...\n";

					// Read bank file
					FileReader fr(e.path());
					{
						RiffReader rr(fr);
						auto ck = rr.readChunk();
						if (!seek_sound_table(rr, ck.getDataEnd()))
						{
							std::cout << "\tCouldn't locate sound table, skipping\n";
							continue;
						}
					}
					fr.skip(8);
					const auto st_offset = fr.getPosition();
					SoundTable st;
					if (!st.read(fr))
					{
						std::cout << "\tCouldn't read sound table, skipping\n";
						continue;
					}
					const auto rest_offset = fr.getPosition();
					const auto rest_size = fr.getRemainingBytes();

					// Replace keys
					{
						size_t replaced = 0;
						size_t unknowns = 0;
						for (auto& e : st.vec)
						{
							if (auto r = map.find(e.first); r != map.end())
							{
								//std::cout << "\t" << e.first << " -> " << r->second << "\n";
								e.first = r->second;
								++replaced;
							}
							else
							{
								//std::cout << "\tunknown hash: " << e.first << "\n";
								++unknowns;
							}
						}
						std::cout << "\t" << replaced << " samples replaced";
						if (unknowns != 0)
						{
							std::cout << "; " << unknowns << " unknowns left dangling";
						}
						std::cout << "\n";
					}

					// Re-sort sound table to keep binary search working
					std::sort(st.vec.begin(), st.vec.end(), [](const std::pair<uint64_t, uint32_t>& a, const std::pair<uint64_t, uint32_t>& b)
					{
						return a.first < b.first;
					});

					// Write replacement file
					std::filesystem::rename(dest_file, path / "SchwapBackups" / (bank_name + "_" + game_lang + ".assets.bank - original"));
					{
						FileWriter fw(dest_file);
						std::string buf;
						fr.seekBegin();
						fr.str(st_offset, buf); fw.str(st_offset, buf);
						st.write(fw);
						fr.seek(rest_offset);
						fr.str(rest_size, buf); fw.str(rest_size, buf);
					}
					string::toFile(path / "SchwapBackups" / (bank_name + "_" + game_lang + ".assets.bank - replacement sha1"), sha1_file(dest_file));

				}
			}
		}
	}

	std::cout << "All done. Feel free to close this window now. :)\n";
	system("pause > nul");
	return 0;
}
