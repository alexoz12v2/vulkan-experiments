#!/bin/zsh
# Build a list of files passed as command line arguments to a minimal iOS app, sing it,
# install it, and run it on the device
#   ./ios-run.sh $FILES               build + install + run, stdout/stderr streamed here
#   RUN_DEBUG=1 ./ios-run.sh $FILES   build + install + launch suspended, then attach xcrun lldb

# won't go in details, but basically -e, -u, -o pipefail, ensure a "use strict"; for shell scripts
set -euo pipefail

# $0: The path of the script being executed.
# :A: A Zsh-specific modifier that resolves the absolute path, expanding any symbolic links along the way.
# :h: A Zsh-specific modifier that grabs the "head" of the path (the directory containing the file, stripping away the filename itself).
# cd ${0:A:h} # I don't think we necessarely need to change current directory

require_var() {
  local var_name="$1"

  # ${{(P)var_name}} is zsh indirect variable syntax
  if [ -z "${(P)var_name:-}+x" ]; then
    echo "$var_name variable unset. run 'xcrun devicectl list devices' and select an Identifier"
    return 1
  fi
}

# Should be device identifier obtained from `xcrun devicectl list devices`
require_var "DEVICE" || exit 1
# Bundle Identifier, for which You've created an embedded profile
require_var "BID" || exit 1

require_var "TEAM" || exit 1

require_var "IDENTITY" || exit 1

# PROFILE may be optional, as we can infer it from a provisioning data file which contains
# TEAM and BID
PROFILE=${PROFILE:-$(grep -l "$TEAM.$BID" ~/Library/Developer/Xcode/UserData/Provisioning\ Profiles/*.mobileprovision | head -1)}
if [[ -z "$PROFILE" ]]; then 
  echo "PROFILE Empty"
  exit 1
fi

# relative path and name of the app. Relative to current working directory
APP=build/cli.app

# define compilation alias
CC=(xcrun -sdk iphoneos clang -arch arm64 -miphoneos-version-min=17.0)

##########################
# compilation
##########################

# generate a separate object file, the debug map points to it
mkdir -p build $APP
objs=()
for src in "$@"; do 
  obj=build/${src:t:r}.o # tail minus extension (TODO anki card)
  $CC -O0 -g -c "$src" -o "$obj"
  objs+=("$obj")
done
$CC "${objs[@]}" -o $APP/cli

# keep debug info on the host for lldb
dsymutil $APP/cli -o build/cli.dSYM

##########################
# plist
##########################
cat > $APP/Info.plist <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleIdentifier</key><string>$BID</string>
  <key>CFBundleExecutable</key><string>cli</string>
  <key>CFBundleName</key><string>cli</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleVersion</key><string>$(date +%s)</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundleSupportedPlatforms</key><array><string>iPhoneOS</string></array>
  <key>MinimumOSVersion</key><string>17.0</string>
  <key>UIDeviceFamily</key><array><integer>1</integer><integer>2</integer></array>
</dict></plist>
EOF

# TODO: run `plutil -lint` to validate plist files

cat > build/ent.plist <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>application-identifier</key><string>$TEAM.$BID</string>
  <key>com.apple.developer.team-identifier</key><string>$TEAM</string>
  <key>get-task-allow</key><true/>
</dict></plist>
EOF

cp "$PROFILE" $APP/embedded.mobileprovision
codesign -f -s "$IDENTITY" --entitlements build/ent.plist --timestamp=none $APP
xcrun devicectl device install app --device $DEVICE $APP

# environment variable run or debug
if [[ ${RUN_DEBUG:-0} == 1 ]]; then
  xcrun devicectl device process launch --device $DEVICE --terminate-existing --start-stopped --json-output build/launch.json $BID
  # TODO: explore this command in depth
  PID=$(plutil -extract result.process.processIdentifier raw build/launch.json)
  echo "launch suspended, pid $PID"
  xcrun lldb $APP/cli -o "device select $DEVICE" -o "device process attach -p $PID" -o "breakpoint set -n main"
else
  xcrun devicectl device process launch --device $DEVICE --terminate-existing --console $BID
fi

