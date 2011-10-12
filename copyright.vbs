' Walk the source tree updating copyright statements for source files.
'
' This uses the Doxygen @author string to locate which specific source files
' need a license injected.
'
' The particular point in using the @author string is that it denotes an ideal
' point to insert the copyright string, and it ensures we only mark things
' that are suitable for such marking and not XML documents like project files

Dim fso
Set fso = CreateObject ("Scripting.FileSystemObject")

Dim root
Set root = fso.GetFolder ("C:\work\steamlimit")

Dim paths
Set paths = new RegExp
paths.Pattern = "^[a-zA-Z0-9]+(\.(c|cpp|h)|)$"

Dim author
Set author = new RegExp
author.Pattern = "(@author (Nigel Bree).*)\n"

Dim oldCopy
Set oldCopy = new RegExp
oldCopy.Pattern = "( \* @author .*)\n( \*.*\r\n)*( \* .*SUCH DAMAGE\.\r\n)"

dim lf
lf = Chr (10)

dim crlf
crlf = Chr (13) + Chr (10)

dim copyText
copyText = " * Copyright (C) 2011 Nigel Bree; All Rights Reserved." + crlf
copyText = copytext + _
" * " + crlf + _
" * Redistribution and use in source and binary forms, with or without" + crlf + _
" * modification, are permitted provided that the following conditions" + crlf + _
" * are met:" + crlf + _
" * " + crlf + _
" * Redistributions of source code must retain the above copyright notice," + crlf + _
" * this list of conditions and the following disclaimer." + crlf + _
" * " + crlf + _
" * Redistributions in binary form must reproduce the above copyright notice," + crlf + _
" * this list of conditions and the following disclaimer in the documentation" + crlf + _
" * and/or other materials provided with the distribution." + crlf + _
" * " + crlf + _
" * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS" + crlf + _
" * ""AS IS"" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT" + crlf + _
" * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR" + crlf + _
" * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT" + crlf + _
" * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,INDIRECT, INCIDENTAL," + crlf + _
" * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED" + crlf + _
" * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR" + crlf + _
" * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF" + crlf + _
" * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING" + crlf + _
" * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS" + crlf + _
" * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE." + crlf

labelCode root

' Recurse through a subdirectory tree looking for source files.

Sub labelCode (folder)
  ' Skip folder names like .hg
  If Left (folder.Name, 1) = "." Then
    Exit Sub
  End If

  Dim file
  For Each file in folder.Files
    ' Match the file name against the known source file types
    If paths.Test (file.Name) Then
      labelFile file
    End If
  Next

  Dim subFolder
  For Each subFolder in folder.SubFolders
    labelCode subFolder
  Next
End Sub

' Having found a source file, attempt to label it with the copyright string
' we want to use.

Sub labelFile (file)
  Dim text
  text = fso.OpenTextFile (file.Path, 1).ReadAll

  If Not author.Test (text) Then
    Exit Sub
  End If

  WScript.Echo file.Path

  ' Strip any existing copyright string
  text = oldCopy.Replace (text, "$1" + lf)

  ' Insert new copyright string
  text = author.Replace (text, "$1" + lf + " *" + crlf + copyText)

  ' Write the new content
  fso.OpenTextFile (file.Path, 2).Write (text)

  WScript.Echo file.Path
End Sub
