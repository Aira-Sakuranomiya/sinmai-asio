# SPDX-License-Identifier: GPL-3.0-only
param([Parameter(Mandatory=$true)][string]$Path)
$ErrorActionPreference='Stop'
$s=Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
function Percentile($histogram,[double]$fraction) {
    $total=($histogram | Measure-Object -Sum).Sum
    if(-not $total){return $null}
    $threshold=[Math]::Ceiling($total*$fraction);$sum=0
    for($i=0;$i -lt $histogram.Count;$i++){$sum+=$histogram[$i];if($sum -ge $threshold){if($i -eq 63){return '>=15.75 (saturated)'};return (($i+1)*0.25)}}
}
[pscustomobject]@{
    Mode=$s.source;State=$s.state;Fault=$s.fault;ElapsedSeconds=$s.elapsedSeconds
    AcceptedCriFrames=$s.criFrames;RenderedFrames=$s.renderFrames;AudibleFrames=$s.nonzeroFrames
    AsioFrames=$s.frames;MissingFramesIncludingWarmup=$s.missingSamplesIncludingWarmup
    MissingAfterWarmup=$s.missingAfterWarmup;NonzeroOutputFrames=$s.outputNonzeroFrames
    MaximumQueueFrames=$s.peakQueue;Channels=$s.channels;ProducerThreadChanges=$s.threadChanges
    RenderToCopyMedianUpperMs=(Percentile $s.waitHistogram250us 0.5)
    RenderToCopyP99UpperMs=(Percentile $s.waitHistogram250us 0.99)
    RenderGapP99UpperMs=(Percentile $s.criGapHistogram250us 0.99)
    DriverReportedOutputMs=$s.reportedOutputMs;MaximumAsioCallbackUs=$s.maxCallbackUs
    MaximumRenderLockUs=$s.maximumRenderLockUs;MaximumRenderWorkUs=$s.maximumRenderWorkUs
    Note='Histogram is software render-to-ASIO-copy only; not physical output latency.'
} | Format-List
