import bs4
import os
import sys
import re


# find number of visits
# find number of carers

class Carer:
    def __init__(self, id, sap_number):
        self.id = id
        self.sap_number = sap_number


class Visit:
    def __init__(self, assigned_carer):
        self.assigned_carer = assigned_carer


def load_problem(filepath):
    with open(filepath, 'r') as fp:
        soup = bs4.BeautifulSoup(fp, "html5lib")

        attributes = {}
        for node in soup.find_all('attribute'):
            attributes[node['title']] = node['id']

        type_id = attributes['type']
        sap_number_id = attributes['sap_number']
        id_id = attributes['id']
        assigned_carer_id = attributes['assigned_carer']

        carers = []
        visits = []
        for node in soup.find_all('node'):
            attributes = node.find('attvalues')
            type_attr = attributes.find('attvalue', attrs={'for': type_id})
            if type_attr['value'] == 'carer':
                id_attr = attributes.find('attvalue', attrs={'for': id_id})
                sap_number_attr = attributes.find('attvalue', attrs={'for': sap_number_id})
                carers.append(Carer(id_attr['value'], sap_number_attr['value']))
            elif type_attr['value'] == 'visit':
                assigned_carer_attr = attributes.find('attvalue', attrs={'for': assigned_carer_id})
                visit = Visit(assigned_carer_attr['value']) if assigned_carer_attr else Visit(None)
                visits.append(visit)
        return carers, visits


if __name__ == '__main__':
    if len(sys.argv) <= 1:
        print('Usage: {0} solution_directory'.format(__file__), file=sys.stderr)

    problems = []
    problem_dir = sys.argv[1]
    name_pattern = re.compile("^.*?_(?P<problem_name>\w\d+)[^\d].*$")
    for file in os.listdir(problem_dir):
        if file.endswith('.gexf'):
            match = name_pattern.match(file)
            problem_name = match.group('problem_name')
            problem_path = os.path.join(problem_dir, file)
            problems.append((problem_name, problem_path))

    for problem, path in problems:
        carers, visits = load_problem(path)
        used_carers = set()
        for visit in visits:
            used_carers.add(visit.assigned_carer)
        dropped_carers = [carer for carer in carers if carer.id not in used_carers]
        dropped_visits = [visit for visit in visits if not visit.assigned_carer]
        print('{0}, {1}, {2}, {3}, {4}'.format(problem,
                                               len(carers),
                                               len(visits),
                                               len(dropped_carers),
                                               len(dropped_visits)))
