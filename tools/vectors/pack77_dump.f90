! pack77_dump.f90 — dumps pack77/unpack77 oracle vectors from WSJT-X's packjt77.
! Reads a scenario file line by line; state (hash tables, mycall, dxcall) evolves
! exactly as inside WSJT-X, so the consumer must replay the lines in order.
!   #MYCALL <call>      set mycall13           (echoed)
!   #DXCALL <call>      set dxcall13           (echoed)
!   #NRX 0|1            nrx for unpack77       (echoed)
!   #RANDOM n seed      unpack n pseudo-random 77-bit words -> U|c77|msg|ok
!   U <77 bits>         unpack the given word  -> U|c77|msg|ok
!   # ...               comment (echoed)
!   <message>           fmtmsg + pack77 + unpack77 -> P|orig|i3|n3|c77|msg|ok
program pack77_dump
  use packjt77
  implicit none
  character*80 line
  character*37 msg, msgout
  character*77 c77
  character*256 infile
  integer i3, n3, iz, nrx, ios, i, j, nrand
  integer*8 x, seed
  logical ok

  if (iargc() .ne. 1) stop 'usage: pack77_dump <scenario.txt>'
  call getarg(1, infile)
  open(10, file=infile, status='old')
  nrx = 1
  do
    read(10, '(a80)', iostat=ios) line
    if (ios .ne. 0) exit
    if (len_trim(line) .eq. 0) cycle
    if (line(1:1) .eq. '#') then
      write(*, '(a)') trim(line)
      if (line(1:8) .eq. '#MYCALL ') then
        mycall13 = line(9:21)
      else if (line(1:8) .eq. '#DXCALL ') then
        dxcall13 = line(9:21)
      else if (line(1:5) .eq. '#NRX ') then
        read(line(6:), *) nrx
      else if (line(1:8) .eq. '#RANDOM ') then
        read(line(9:), *) nrand, seed
        x = seed
        do i = 1, nrand
          do j = 1, 77
            x = mod(x * 48271_8, 2147483647_8)      ! Park-Miller minimal standard
            c77(j:j) = '0'
            if (mod(x / 1024_8, 2_8) .eq. 1_8) c77(j:j) = '1'
          enddo
          call unpack77(c77, nrx, msgout, ok)
          write(*, '(a,a,a,a,a,l1)') 'U|', c77, '|', trim(msgout), '|', ok
        enddo
      endif
      cycle
    endif
    if (line(1:2) .eq. 'U ') then
      c77 = line(3:79)
      call unpack77(c77, nrx, msgout, ok)
      write(*, '(a,a,a,a,a,l1)') 'U|', c77, '|', trim(msgout), '|', ok
      cycle
    endif
    write(*, '(a,a,a)', advance='no') 'P|', trim(line), '|'
    call fmtmsg(line, iz)
    msg = line(1:37)
    i3 = -1
    n3 = -1
    call pack77(msg, i3, n3, c77)
    call unpack77(c77, nrx, msgout, ok)
    write(*, '(i0,a,i0,a,a,a,a,a,l1)') i3, '|', n3, '|', c77, '|', trim(msgout), '|', ok
  enddo
end program pack77_dump
