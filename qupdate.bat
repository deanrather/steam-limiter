call hg qpop -a
if errorlevel 1 goto :eof
call hg pull --mq
call hg update --mq
call hg qpush -a
