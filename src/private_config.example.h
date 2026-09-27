// Copy to private_config.h (same folder) and set your own password hash.
// private_config.h is gitignored.
//
// Password for uploading a firmware .bin on the clock's web page (/update).
// Put its SHA-256 (lower-case hex) here, e.g.:  printf '%s' 'my-long-password' | sha256sum
// Use a long random password: the hash ends up inside the public firmware.bin.
// Empty = manual upload disabled (updates from GitHub still work).
#define UPDATE_PASS_SHA256 ""
