param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$IdfArguments
)

& idf.py -B build_pod -D SITETWIN_DEVICE_ROLE=pod @IdfArguments
exit $LASTEXITCODE
