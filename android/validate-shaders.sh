#!/usr/bin/env bash
# Validates every game shader as GLSL ES 3.20 (Android) and as GLSL 4.60
# (desktop), assembled the way the game assembles them: _geometry.glsl prefix + shader.
# On ES the prefix has its #version/#extension replaced by the same header that
# ogl::set_shader_prefix uses on Android (opengl_wrapper.cpp) -- keep the two the same.
#
# Usage: android/validate-shaders.sh   (needs glslangValidator: apt install glslang-tools)
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$ROOT/assets/shaders/glsl"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

ES_HEADER='#version 320 es
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp sampler2DArray;
precision highp samplerBuffer;
precision highp isamplerBuffer;
precision highp usampler2D;'

ES_PREFIX="$ES_HEADER"$'\n'"$(grep -v -E '^\s*#(version|extension)' "$DIR/_geometry.glsl")"
DESKTOP_PREFIX="$(cat "$DIR/_geometry.glsl")"

failures=0
for mode in es desktop; do
	mkdir -p "$OUT/$mode"
	prefix=$([ "$mode" = es ] && printf '%s' "$ES_PREFIX" || printf '%s' "$DESKTOP_PREFIX")
	for f in "$DIR"/*.glsl; do
		name=$(basename "$f" .glsl)
		[ "$name" = _geometry ] && continue
		case "$name" in *_v|*_v_*|*_v_shader|screen_v) stage=vert ;; *) stage=frag ;; esac
		printf '%s\n%s\n' "$prefix" "$(cat "$f")" > "$OUT/$mode/$name.$stage"
		if ! result=$(glslangValidator "$OUT/$mode/$name.$stage" 2>&1); then
			failures=$((failures + 1))
			echo "FAILED ($mode) $name.$stage"
			echo "$result" | grep ERROR | head -5
		fi
	done
done

# vertex+fragment pairs that the game links (map.cpp and opengl_wrapper.cpp)
for pair in "map_v map_f" "map_v map_provinces_f" "textured_line_v textured_line_f" \
	"textured_line_variable_width_v textured_line_river_f" "trade_route_v trade_route_f" \
	"textured_line_v wavy_textured_line_f" "textured_line_b_v textured_line_b_f" \
	"textured_line_b_v textured_line_b_provinces_f" "line_unit_arrow_v line_unit_arrow_f" \
	"map_font_v map_font_f" "screen_v white_color_f" "model3d_v model3d_f" "map_sprite_v map_sprite_f" \
	"map_triangle_v map_triangle_f" "debug_map_triangle_v debug_map_triangle_f" \
	"ui_v_shader ui_f_shader" "msaa_v_shader msaa_f_shader"; do
	set -- $pair
	if ! result=$(glslangValidator -l "$OUT/es/$1.vert" "$OUT/es/$2.frag" 2>&1); then
		failures=$((failures + 1))
		echo "FAILED (es linking) $1 + $2"
		echo "$result" | grep ERROR | head -5
	fi
done

if [ "$failures" -eq 0 ]; then
	echo "All shaders valid as GLSL ES 3.20 and GLSL 4.60."
else
	echo "$failures failure(s)."
	exit 1
fi
