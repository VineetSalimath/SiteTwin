param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$IdfArguments
)

& idf.py -B build_gateway -D SITETWIN_DEVICE_ROLE=gateway @IdfArguments
exit $LASTEXITCODE
