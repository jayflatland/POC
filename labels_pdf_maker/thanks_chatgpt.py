from reportlab.lib.pagesizes import letter
from reportlab.lib.units import inch
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer

# create a function to generate the PDF
def generate_pdf(data):
  # create a PDF document with the correct page size
  doc = SimpleDocTemplate("address_labels.pdf", pagesize=letter)
  # create an empty list to hold the label elements
  elements = []
  # iterate over the data and create a label for each item
  for item in data:
    # create a paragraph with the address information
    addr = Paragraph(item)#, style=style)
    # add the address to the list of label elements
    elements.append(addr)
    # add a spacer below the address
    elements.append(Spacer(1, 0.2 * inch))
  # build the PDF with the label elements
  doc.build(elements)

# example usage
data = ["John Doe", "123 Main St", "Anytown, USA 12345"]
generate_pdf(data)
