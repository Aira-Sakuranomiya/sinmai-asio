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
    BufferSamples=$s.bufferSamples;ConfiguredServerFrequency=$s.configuredServerFrequency;DiagnosticFrequencyOverride=$s.serverFrequencyOverride
    Mode=$s.source;State=$s.state;Fault=$s.fault;ElapsedSeconds=$s.elapsedSeconds
    AcceptedCriFrames=$s.criFrames;RenderedFrames=$s.renderFrames;AudibleFrames=$s.nonzeroFrames
    AsioFrames=$s.frames;MissingFramesIncludingWarmup=$s.missingSamplesIncludingWarmup
    MissingAfterWarmup=$s.missingAfterWarmup;NonzeroOutputFrames=$s.outputNonzeroFrames
    InputPeakBeforeLimiter=$s.inputPeak;OverRangeFrames=$s.overRangeFrames
    LimitedFrames=$s.limitedFrames;InvalidSamples=$s.invalidSamples
    MaximumQueueFrames=$s.peakQueue;Channels=$s.channels;ProducerThreadChanges=$s.threadChanges
    RenderToCopyMedianUpperMs=(Percentile $s.waitHistogram250us 0.5)
    RenderToCopyP99UpperMs=(Percentile $s.waitHistogram250us 0.99)
    RenderGapP99UpperMs=(Percentile $s.criGapHistogram250us 0.99)
    DriverReportedOutputMs=$s.reportedOutputMs;MaximumAsioCallbackUs=$s.maxCallbackUs
    MaximumRenderLockUs=$s.maximumRenderLockUs;MaximumRenderWorkUs=$s.maximumRenderWorkUs
    MaximumAsioCallbackGapUs=$s.callbackMaxGapUs;MaximumAsioCallbackGapExcessUs=$s.callbackMaxGapExcessUs
    AsioGapComparisons=$s.callbackGapCount;GapsOver150Percent=$s.callbackGapsOver150Percent;GapsOver200Percent=$s.callbackGapsOver200Percent
    TimeInfoCallbacks=$s.timeInfoCallbacks;LegacyCallbacks=$s.legacyCallbacks
    ValidPositionCallbacks=$s.validPositionCallbacks;UnavailablePositionCallbacks=$s.unavailablePositionCallbacks
    PositionDiscontinuities=$s.positionDiscontinuities;PositionForwardGapSamples=$s.positionForwardGapSamples
    PositionBackwardSteps=$s.positionBackwardSteps;PositionRepeatedSteps=$s.positionRepeatedSteps;PositionShortSteps=$s.positionShortSteps
    DriverResyncRequests=$s.resyncRequests;DriverResetRequests=$s.resetRequests
    DriverBufferChangeRequests=$s.bufferChangeRequests;DriverLatencyChangeRequests=$s.latencyChangeRequests
    Note='Histogram is software render-to-ASIO-copy only; not physical output latency.'
} | Format-List
