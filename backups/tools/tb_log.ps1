$keys='act','vz','pin','sims','warm','ride','rt','hsup','minsup','vin','gap','dface','duse','us','cap','mindz','bok','room','rty','rtp','cfwd','pinf','pina'
$rows=@()
foreach($ln in (Get-Content C:\dih\debug.log)){
  if($ln -notmatch '^TB: act=') { continue }
  $o=[ordered]@{}
  foreach($k in $keys){ if($ln -match "(?:^TB: | )$k=(\S+)"){ $o[$k]=$matches[1] } }
  $rows+=[pscustomobject]$o
}
"rows=$($rows.Count)"
"acts: " + (($rows | group act | %{ "$($_.Name)=$($_.Count)" }) -join ' ')
# max consecutive B
$mx=0;$c=0; foreach($r in $rows){ if($r.act -eq 'B'){$c++; if($c -gt $mx){$mx=$c}} else {$c=0} }; "maxBrun=$mx"
# ride runs
$runs=@();$c=0; foreach($r in $rows){ if($r.ride -eq '1'){$c++} elseif($c){$runs+=$c;$c=0} }; if($c){$runs+=$c}
"ride runs: " + ($runs -join ',')
# C followed by B vs not
$cb=0;$cn=0; for($i=0;$i -lt $rows.Count-1;$i++){ if($rows[$i].act -eq 'C'){ if($rows[$i+1].act -eq 'B'){$cb++}else{$cn++} } }; "C->B=$cb C->other=$cn"
# C after landed pin (vz > -20) : followed by B?
$lb=0;$ln2=0; for($i=1;$i -lt $rows.Count-1;$i++){ if($rows[$i].act -eq 'C' -and $rows[$i-1].act -eq 'B'){ if($rows[$i+1].act -eq 'B'){$lb++}else{$ln2++} } }; "B,C->B=$lb B,C->notB=$ln2"
# B->B transitions
$bb=0;$bn=0; for($i=0;$i -lt $rows.Count-1;$i++){ if($rows[$i].act -eq 'B'){ if($rows[$i+1].act -eq 'B'){$bb++}else{$bn++} } }; "B->B=$bb B->other=$bn"
"cap1=" + @($rows | ?{ $_.cap -eq '1' }).Count
"maxUs=" + (($rows | %{ [int]$_.us } | measure -Maximum).Maximum)
"dface>=duse=" + @($rows | ?{ [double]$_.dface -ge [double]$_.duse }).Count
"hsup>=0.2 (C/H/D)=" + @($rows | ?{ $_.act -in 'C','H','D' -and [double]$_.hsup -ge 0.2 }).Count
"--- per row around B ---"
for($i=0;$i -lt $rows.Count;$i++){ $r=$rows[$i]
  "{0,3} {1} vz={2,7} pin={3} sims={4,3} warm={5} ride={6} rt={7} hsup={8} minsup={9} vin={10} gap={11} room={12} dface={13} mindz={14} bok={15} us={16} rty={17} rtp={18} cfwd={19} pinf={20} pina={21}" -f $i,$r.act,$r.vz,$r.pin,$r.sims,$r.warm,$r.ride,$r.rt,$r.hsup,$r.minsup,$r.vin,$r.gap,$r.room,$r.dface,$r.mindz,$r.bok,$r.us,$r.rty,$r.rtp,$r.cfwd,$r.pinf,$r.pina }

# ---- TB REF: / TB RESC: (dih150+) ----
$rk='act','vz','mindz','minsup','bsup','fwd','ang','sims','ride','xy','nx','ny','us','cap','ox','oy','oz','pz','wz','pd','wfr','vx','vy','duck','hz','plan','pol','t','w','hold','rp'
$REF=@()
foreach($ln in (Get-Content C:\dih\debug.log)){
  if($ln -notmatch '^TB (REF|S): ') { continue }
  $o=[ordered]@{}
  foreach($k in $rk){ if($ln -match "(?:^TB (?:REF|S): | )$k=(\S+)"){ $o[$k]=$matches[1] } }
  $REF+=[pscustomobject]$o
}
"=== REF rows=$($REF.Count)  acts: " + (($REF | group act | %{ "$($_.Name)=$($_.Count)" }) -join ' ')
if($REF.Count){
  "duck vals: " + (($REF | group duck | %{ "$($_.Name)=$($_.Count)" }) -join ' ') + "  hz vals: " + (($REF | group hz | %{ "$($_.Name)=$($_.Count)" }) -join ' ')
  $pb=$null
  for($i=0;$i -lt $REF.Count;$i++){ $r=$REF[$i]
    $along = if([math]::Abs([double]$r.nx) -gt [math]::Abs([double]$r.ny)){ [double]$r.oy } else { [double]$r.ox }
    $dxy = if($i){ [double]$REF[$i-1].xy - [double]$r.xy } else { 0 }
    $ex=''
    if($r.act -eq 'B'){
      if($pb){ $ex = "dAlong={0:N3} dTick={1} dOz={2:N3}" -f ($along-$pb.al),($i-$pb.i),([double]$r.oz-$pb.oz) }
      $pb=@{al=$along;i=$i;oz=[double]$r.oz}
    }
    if($r.act -ne 'B' -and $i -and $REF[$i-1].act -eq 'B'){ $ex = "LANDED drop(oz-pz)={0:N5} wz-oz={1:N5}" -f ([double]$r.oz-[double]$r.pz),([double]$r.wz-[double]$r.oz) }
    "{0,3} {1} vz={2,7} mindz={3,8} ms={4} fwd={5} ang={6} sims={7,3} ride={8} along={9:N3} ox={10} oz={11} pz={12} wz={13} pd={14} xy={15} dxy={16:N2} vx={17} duck={18} hz={19} us={20} plan={21} pol={22} t={23} {24}" -f $i,$r.act,$r.vz,$r.mindz,$r.minsup,$r.fwd,$r.ang,$r.sims,$r.ride,$along,$r.ox,$r.oz,$r.pz,$r.wz,$r.pd,$r.xy,$dxy,$r.vx,$r.duck,$r.hz,$r.us,$r.plan,$r.pol,$r.t,$ex
  }
  # dih152: ride = run of B/L ticks; did L predictions come true (L t=n -> B n-1 ticks later)?
  $runs=@();$c=0; foreach($r in $REF){ if($r.act -in 'B','L'){$c++} elseif($c){$runs+=$c;$c=0} }; if($c){$runs+=$c}
  "B/L runs: " + ($runs -join ',') + "  maxUs=" + (($REF | %{ [int]$_.us } | measure -Maximum).Maximum)
  $ok=0;$bad=0; for($i=0;$i -lt $REF.Count;$i++){ $r=$REF[$i]; if($r.act -eq 'L' -and $r.t){ $j=$i+[int]$r.t-1; if($j -lt $REF.Count -and $REF[$j].act -eq 'B'){$ok++}else{$bad++} } }
  "dih153 hold=1 rows=" + @($REF | ?{ $_.hold -eq '1' }).Count + " duck=1 rows=" + @($REF | ?{ $_.duck -eq '1' }).Count + " replay rows=" + @($REF | ?{ $_.rp -eq '1' }).Count + " replay B=" + @($REF | ?{ $_.rp -eq '1' -and $_.act -eq 'B' }).Count
  "L predicted B landed=$ok missed=$bad   last policy wins (graze,graze-d,into,into-d,brk60,brk200,brk450,brk450-d,bi,bi-d)=" + $REF[-1].w
}
$RS = @(Get-Content C:\dih\debug.log | ?{ $_ -match '^TB RESC: ' })
"=== RESC rows=$($RS.Count) pinned=" + @($RS | ?{ $_ -match ' pinned=1' }).Count
$RS | ?{ $_ -match ' pinned=1' } | %{ if($_ -match ' pass=(\d)'){ $matches[1] } } | group | %{ "  pass$($_.Name) pinned=$($_.Count)" }
