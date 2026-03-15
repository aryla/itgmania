#!/bin/bash
name="$1"
color="$2"
if [[ -z "$name" || -z "$color" ]]; then
	printf 'USAGE: %s NAME COLOR' "$0" >&2
	exit 1
fi

output="./variants/$name"
mkdir -p "$output"

tmp="$(mktemp -d)"
cleanup() {
	rm -rf -- "$tmp"
}
trap cleanup EXIT

magick ./_Tails.png -colorspace sRGB \( +clone -fill "$color" -colorize 100 \) -compose multiply -composite "$tmp/_Tails.png"
for button in Down Left Up Right; do
	echo "$output/_$button Tap Note 16x16 (doubleres).png"
	magick "$tmp/_Tails.png" "./_$button Tap Note 16x16 (doubleres).png" -composite "$output/_$button Tap Note 16x16 (doubleres).png"
done

parts=(
	'Down Hold Body Active (res 64x256).png'
	'Down Hold Body Inactive (res 64x256).png'
	'Down Hold BottomCap Active (res 64x64).png'
	'Down Hold BottomCap Inactive (res 64x64).png'
	'Down Roll Body active (res 64x256).png'
	'Down Roll Body Inactive (res 64x256).png'
	'Down Roll BottomCap Active (res 64x64).png'
	'Down Roll BottomCap Inactive (res 64x64).png'
)
for part in "${parts[@]}"; do
	echo "$output/$part"
	magick "./$part" -colorspace sRGB \( +clone -fill "$color" -colorize 100 \) -compose multiply -composite "$output/$part"
done
