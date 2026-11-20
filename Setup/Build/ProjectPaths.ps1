#requires -Version 5.1

function Convert-GeneratedProjectPaths {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][ValidatePattern('^[A-Za-z]:$')][string]$TemporaryDrive
    )

    $document = New-Object Xml.XmlDocument
    $document.PreserveWhitespace = $true
    $document.Load($Path)

    foreach ($node in $document.SelectNodes('//@* | //text()')) {
        $element = if ($node -is [Xml.XmlAttribute]) { $node.OwnerElement } else { $node.ParentNode }

        # Filter identifiers are virtual folder names, not filesystem paths.
        if ($element.LocalName -eq 'Filter') { continue }

        # Use the same plain relative item paths in .vcxproj and .filters.
        # Build properties and commands keep ProjectDir to anchor their paths.
        $itemInclude = $node -is [Xml.XmlAttribute] -and $node.LocalName -eq 'Include' -and
            $element.ParentNode.LocalName -eq 'ItemGroup'
        $relativeRoot = if ($itemInclude) { '..\..' } else { '$(ProjectDir)..\..' }
        $value = $node.Value.Replace("$TemporaryDrive\", "$relativeRoot\")
        $value = $value.Replace("$TemporaryDrive/", ($relativeRoot.Replace('\', '/') + '/'))
        $node.Value = $value.Replace($TemporaryDrive, $relativeRoot)
    }

    foreach ($element in $document.SelectNodes('//*[local-name()="Filter"]')) {
        $name = if ($element.HasAttribute('Include')) { $element.GetAttribute('Include') } else { $element.InnerText }
        if ($name -match '\$\(|:|(^|[\\/])\.\.?([\\/]|$)') {
            throw "Invalid virtual folder '$name' in $Path. Configure Premake vpaths for this file."
        }
    }
    if ($document.OuterXml.Contains($TemporaryDrive)) {
        throw "Temporary drive reference remains in $Path."
    }

    $settings = New-Object Xml.XmlWriterSettings
    $settings.Encoding = [Text.UTF8Encoding]::new($false)
    $settings.Indent = $false
    $settings.NewLineHandling = [Xml.NewLineHandling]::None
    $writer = [Xml.XmlWriter]::Create($Path, $settings)
    try {
        $document.Save($writer)
    } finally {
        $writer.Dispose()
    }
}
