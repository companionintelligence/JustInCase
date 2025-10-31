KEYCHAIN=~/Library/Keychains/login.keychain-db
security dump-keychain -d "$KEYCHAIN" \
  | awk '/^\s*0x[0-9a-f]+/{rec=""} {rec=rec $0 ORS} /"svce"<blob>/{if(tolower(rec) ~ /copilot/) print rec "\n---"}' \
  | sed -n 's/.*"labl"<blob>="\(.*\)".*/Label: \1/p; s/.*"svce"<blob>="\(.*\)".*/Service: \1/p; s/.*"acct"<blob>="\(.*\)".*/Account: \1/p; /---/p'

