from PIL import Image
import io

# Load the PNG image in 1-bit color mode
image_path = "test.png"  # Update this path with your PNG image
img = Image.open(image_path).convert("1")

# Save the image in G4 compressed TIFF format in memory
with io.BytesIO() as output:
    img.save(output, format="TIFF", compression="group4")
    tiff_data = output.getvalue()

# Convert TIFF data to C-style byte array format
output_lines = [
    "// Example 128x250 bitmap data in G4 compressed TIFF format",
    "const uint8_t example_bitmap[] = {"
]

# Add the TIFF header and data in hexadecimal format
for i, byte in enumerate(tiff_data):
    if i % 8 == 0:
        output_lines.append("    ")
    output_lines[-1] += f"0x{byte:02X}, "
output_lines[-1] = output_lines[-1].rstrip(", ")  # Remove trailing comma
output_lines.append("};")

# Print the final output
print("\n".join(output_lines))
