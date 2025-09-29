# Download Source Changes Feature

## Overview

The **Download Source Changes** feature allows you to pull source string modifications from Gridly into Unreal Engine 5. This feature downloads source strings from Gridly per namespace, generates CSV files for each string table, and provides instructions for importing them into UE5's string table system.

## ⚠️ Important Notes

- **This feature modifies source strings** in your localization files
- **Review all changes** before committing to version control
- **Backup your localization files** before using this feature
- **This goes against UE's "author-at-source" philosophy** - use with caution

## How It Works

1. **Downloads source strings** from Gridly for the native culture
2. **Groups records by namespace** (using the path column)
3. **Generates CSV files** for each namespace with format: `Key,SourceString`
4. **Stores files** in `[Project]/Saved/Temp/GridlySourceChanges/[TargetName]/`
5. **Automatically imports** CSV data into UE5 localization manifests
6. **Updates string tables** with the new source text

## Prerequisites

1. **Gridly Plugin Configuration**:
   - Import API Key configured
   - At least one Import View ID configured
   - Source language column prefix configured
   - **String Table Save Path** (optional): Configure where new string tables should be saved (default: `/Game/Localization/StringTables`)

2. **Localization Target Setup**:
   - Native culture defined
   - Supported cultures configured

## Usage

### Step 1: Configure String Table Save Path (Optional)

1. Go to **Project Settings** > **Plugins** > **Gridly**
2. In the **Options** section, find **"String Table Save Path"**
3. Set your preferred path for new string tables (e.g., `/Game/MyProject/Localization/StringTables`)
4. Leave empty to use the default path: `/Game/Localization/StringTables`

### Step 2: Access the Feature

1. Open the **Localization Dashboard** (`Window > Localization Dashboard`)
2. Select **Gridly** as your Localization Service Provider
3. Select your localization target
4. Click the **"Download Source Changes"** button (4th button in the toolbar)

### Step 2: Confirm the Operation

A confirmation dialog will appear explaining:
- What the feature will do
- Where files will be stored
- Warning about source string modifications

Click **"Yes"** to proceed.

### Step 3: Monitor Progress

The system will:
- Download data from Gridly
- Process records by namespace
- Generate CSV files
- **Automatically import** CSV data into localization manifests
- Show progress in the Output Log

### Step 4: Review Results

After completion, you'll see a success message confirming:
- Number of namespaces processed
- Location of generated CSV files
- **Automatic import completion**
- **String table updates**

**No manual import required!** The source strings have been automatically imported into your localization manifests.

## CSV File Format

Each generated CSV file contains:
```csv
Key,SourceString
"key1","Updated source text 1"
"key2","Updated source text 2"
```

## File Locations

- **CSV Files**: `[Project]/Saved/Temp/GridlySourceChanges/[TargetName]/`
- **Log Output**: UE5 Output Log (Window > Developer Tools > Output Log)

## Error Handling

The feature includes comprehensive error handling for:
- Missing API key or view ID configuration
- No native culture defined
- Network connection issues
- Invalid Gridly response format
- File system errors

## Troubleshooting

### Common Issues

1. **"No import API key configured"**
   - Go to Project Settings > Plugins > Gridly
   - Set the Import API Key

2. **"No import view ID configured"**
   - Go to Project Settings > Plugins > Gridly
   - Add at least one Import View ID

3. **"No native culture found"**
   - Check your localization target settings
   - Ensure a native culture is defined

4. **CSV files not generated**
   - Check the Output Log for error messages
   - Verify Gridly API connectivity
   - Ensure source language columns are configured correctly

5. **String tables created in wrong location**
   - Check the **String Table Save Path** setting in Project Settings > Plugins > Gridly
   - Ensure the path is valid and accessible
   - The path should start with `/Game/` and be relative to the Content directory

### Log Messages

The feature uses visual indicators in log messages:
- ✅ Success operations
- ❌ Error conditions
- 📊 Progress information
- 📄 File operations
- 🏷️ Namespace information
- 📋 Instructions

## Integration with Existing Workflow

This feature complements the existing Gridly plugin functionality:
- **Export to Gridly**: Sends source strings to Gridly
- **Import from Gridly**: Downloads translations
- **Export All to Gridly**: Sends all translations
- **Download Source Changes**: Downloads source string modifications

## Best Practices

1. **Always backup** your localization files before use
2. **Review changes** in the generated CSV files before importing
3. **Test on a non-production project** first
4. **Use version control** to track changes
5. **Coordinate with your team** when using this feature

## Technical Details

### API Endpoint
- **URL**: `https://api.gridly.com/v1/views/{ViewId}/records`
- **Method**: GET
- **Authentication**: API Key in Authorization header

### Data Processing
- Records are grouped by the `path` column (namespace)
- Source strings are extracted from the native culture column
- Combined namespace keys are handled automatically
- CSV files are generated with proper escaping
- **Automatic manifest import** updates localization files directly

### File Management
- Temporary files are stored in the project's Saved directory
- Files are organized by target name and namespace
- Existing files are overwritten (no versioning)
- **Localization manifests are automatically updated** with new source strings

## Future Enhancements

Potential improvements for future versions:
- ~~Automatic string table import integration~~ ✅ **IMPLEMENTED**
- Diff visualization for source string changes
- Backup and restore functionality
- Batch processing for multiple targets
- Integration with UE5's localization commandlets 