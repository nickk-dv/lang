#include "common.h"

u32 hash_fnv1a_32(const StringView& str)
{
	#define FNV_PRIME_32 0x01000193UL
	#define FNV_OFFSET_32 0x811c9dc5UL

	u32 hash = FNV_OFFSET_32;
	for (u32 i = 0; i < str.count; i++)
	{
		hash ^= str.data[i];
		hash *= FNV_PRIME_32;
	}
	return hash;
}

u64 hash_fnv1a_64(const StringView& str)
{
	#define FNV_PRIME_64 0x00000100000001B3UL
	#define FNV_OFFSET_64 0xcbf29ce484222325UL

	u64 hash = FNV_OFFSET_64;
	for (u32 i = 0; i < str.count; i++)
	{
		hash ^= str.data[i];
		hash *= FNV_PRIME_64;
	}
	return hash;
}

bool os_file_read_all(const char* file_path, String* str)
{
	FILE* file;
	fopen_s(&file, file_path, "rb");
	if (!file) return false;

	fseek(file, 0, SEEK_END);
	size_t file_size = (size_t)ftell(file);
	fseek(file, 0, SEEK_SET);

	if (file_size == 0)
	{
		fclose(file);
		return false;
	}

	void* buffer =  malloc(file_size);

	if (buffer == NULL)
	{
		fclose(file);
		return false;
	}
	
	size_t read_size = fread(buffer, 1, file_size, file);
	fclose(file);

	if (read_size != file_size)
	{
		free(buffer);
		return false;
	}
	
	str->data = (u8*)buffer;
	str->count = file_size;
	
	return true;
}

bool match_string_view(StringView& a, StringView& b)
{
	return a == b;
}
