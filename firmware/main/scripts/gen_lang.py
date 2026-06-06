#!/usr/bin/env python3
import argparse
import json
import os

HEADER_TEMPLATE = """// Auto-generated language config
// Language: {lang_code} with en-US fallback
#pragma once

#include <string_view>

#ifndef {lang_code_for_font}
    #define {lang_code_for_font}
#endif

namespace Lang {{
    constexpr const char* CODE = "{lang_code}";

    namespace Strings {{
{strings}
    }}

    namespace Sounds {{
{sounds}
    }}
}}
"""

def load_base_language(assets_dir):
    base_lang_path = os.path.join(assets_dir, 'locales', 'en-US', 'language.json')
    if os.path.exists(base_lang_path):
        try:
            with open(base_lang_path, 'r', encoding='utf-8') as f:
                base_data = json.load(f)
                print(f"Loaded base language en-US with {len(base_data.get('strings', {}))} strings")
                return base_data
        except json.JSONDecodeError as e:
            print(f"Warning: Failed to parse en-US language file: {e}")
    else:
        print("Warning: en-US base language file not found, fallback mechanism disabled")
    return {'strings': {}}

def get_sound_files(directory):
    if not os.path.exists(directory):
        return []
    return [f for f in os.listdir(directory) if f.endswith('.ogg')]

def infer_assets_dir(output_path):
    main_dir = os.path.dirname(output_path)
    if os.path.basename(main_dir) == 'assets':
        main_dir = os.path.dirname(main_dir)
    return os.path.join(main_dir, 'assets')

def generate_header(lang_code, output_path, assets_dir=None, sound_names=None):
    if assets_dir is None:
        assets_dir = infer_assets_dir(output_path)
    
    input_path = os.path.join(assets_dir, 'locales', lang_code, 'language.json')
    
    print(f"Processing language: {lang_code}")
    print(f"Input file path: {input_path}")
    print(f"Output file path: {output_path}")
    
    if not os.path.exists(input_path):
        raise FileNotFoundError(f"Language file not found: {input_path}")
    
    with open(input_path, 'r', encoding='utf-8') as f:
        data = json.load(f)

    if 'language' not in data or 'strings' not in data:
        raise ValueError("Invalid JSON structure")

    base_data = load_base_language(assets_dir)
    
    base_strings = base_data.get('strings', {})
    user_strings = data['strings']
    merged_strings = base_strings.copy()
    merged_strings.update(user_strings)
    
    base_count = len(base_strings)
    user_count = len(user_strings)
    total_count = len(merged_strings)
    fallback_count = total_count - user_count
    
    print(f"Language {lang_code} string statistics:")
    print(f"  - Base language (en-US): {base_count} strings")
    print(f"  - User language: {user_count} strings")
    print(f"  - Total: {total_count} strings")
    if fallback_count > 0:
        print(f"  - Fallback to en-US: {fallback_count} strings")

    strings = []
    sounds = []
    for key, value in merged_strings.items():
        value = value.replace('"', '\\"')
        strings.append(f'        constexpr const char* {key.upper()} = "{value}";')

    current_lang_dir = os.path.join(assets_dir, 'locales', lang_code)
    base_lang_dir = os.path.join(assets_dir, 'locales', 'en-US')
    common_dir = os.path.join(assets_dir, 'common')
    
    base_sounds = get_sound_files(base_lang_dir)
    current_sounds = get_sound_files(current_lang_dir)
    common_sounds = get_sound_files(common_dir)

    if sound_names:
        all_sound_files = set(sound_names)
        missing_sounds = all_sound_files - set(base_sounds) - set(current_sounds)
        if missing_sounds:
            raise FileNotFoundError(f"Required sound files not found: {', '.join(sorted(missing_sounds))}")
    else:
        all_sound_files = set(base_sounds)
        all_sound_files.update(current_sounds)
    
    base_sound_count = len(base_sounds)
    user_sound_count = len(current_sounds)
    common_sound_count = len(common_sounds)
    current_selected_count = len(all_sound_files & set(current_sounds))
    base_selected_count = len(all_sound_files & set(base_sounds))
    sound_fallback_count = len((all_sound_files & set(base_sounds)) - set(current_sounds))
    
    print(f"Language {lang_code} sound statistics:")
    print(f"  - Base language (en-US): {base_selected_count} selected sounds ({base_sound_count} available)")
    print(f"  - User language: {current_selected_count} selected sounds ({user_sound_count} available)")
    print(f"  - Common sounds: {common_sound_count} sounds")
    if sound_fallback_count > 0:
        print(f"  - Sound fallback to en-US: {sound_fallback_count} sounds")
    
    for file in sorted(all_sound_files):
        base_name = os.path.splitext(file)[0]
        if file in current_sounds:
            sound_lang = lang_code.replace('-', '_').lower()
        else:
            sound_lang = 'en_us'
            
        sounds.append(f'''
        extern const char ogg_{base_name}_start[] asm("_binary_{base_name}_ogg_start");
        extern const char ogg_{base_name}_end[] asm("_binary_{base_name}_ogg_end");
        static const std::string_view OGG_{base_name.upper()} {{
        static_cast<const char*>(ogg_{base_name}_start),
        static_cast<size_t>(ogg_{base_name}_end - ogg_{base_name}_start)
        }};''')
    
    for file in sorted(common_sounds):
        base_name = os.path.splitext(file)[0]
        sounds.append(f'''
        extern const char ogg_{base_name}_start[] asm("_binary_{base_name}_ogg_start");
        extern const char ogg_{base_name}_end[] asm("_binary_{base_name}_ogg_end");
        static const std::string_view OGG_{base_name.upper()} {{
        static_cast<const char*>(ogg_{base_name}_start),
        static_cast<size_t>(ogg_{base_name}_end - ogg_{base_name}_start)
        }};''')

    content = HEADER_TEMPLATE.format(
        lang_code=lang_code,
        lang_code_for_font=lang_code.replace('-', '_').lower(),
        strings="\n".join(sorted(strings)),
        sounds="\n".join(sorted(sounds))
    )

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(content)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate language configuration header file with en-US fallback")
    parser.add_argument("--language", required=True, help="Language code (e.g: zh-CN, en-US, ja-JP)")
    parser.add_argument("--output", required=True, help="Output header file path")
    parser.add_argument("--assets-dir", help="Directory containing locales/ and common/ assets")
    parser.add_argument("--sound", action="append", default=[], help="Localized sound file to declare, e.g. wificonfig.ogg")
    args = parser.parse_args()

    try:
        generate_header(args.language, args.output, args.assets_dir, args.sound)
        print(f"Successfully generated language config file: {args.output}")
    except Exception as e:
        print(f"Error: {e}")
        exit(1)
